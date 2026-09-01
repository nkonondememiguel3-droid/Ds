#include "gc.h"

#include <stdio.h>
#include <stdlib.h>

#include "ds_arena.h"

/*
 * Mark and sweep over the arena's inline block headers.
 *
 * There is no side table: every block the arena hands out carries its own
 * descriptor, size and mark bit in the ARENA_ALIGN bytes immediately before
 * the payload, and every byte of a chunk's used region belongs to exactly
 * one block. Marking is therefore a pointer subtraction, and sweeping is a
 * linear walk of each chunk in address order.
 *
 * Everything the collector owns lives in a ds_gc_t attached to the arena's
 * `collector` slot on first use, so an arena that is never collected carries
 * two null pointers and nothing else. The dependency is one-way: this file
 * includes ds_arena.h, and no arena source includes this header.
 */

/* ------------------------------------------------------------------ */
/* Attaching to an arena                                               */
/* ------------------------------------------------------------------ */

ds_gc_t *ds_gc_state_of(const _ds_arena_t_ *a) { return a ? (ds_gc_t *)a->collector : NULL; }

/* Called by ds_arena_destroy. The state and the roots are arena blocks and
 * die with the chunks; only the mark stack is a malloc'd buffer, because it
 * has to grow by realloc and the arena has none. */
static void ds_gc_detach(_ds_arena_t_ *a) {
  ds_gc_t *g = (ds_gc_t *)a->collector;
  if (!g) return;
  free(g->mark_stack);
  g->mark_stack = NULL;
  g->mark_stack_cap = 0;
  g->mark_stack_top = 0;
  g->roots = NULL;
}

static ds_gc_t *ds_gc_attach(_ds_arena_t_ *a) {
  ds_gc_t *g = (ds_gc_t *)a->collector;

  if (!g) {
    /* A raw block: never traced, never swept, freed with the arena. */
    g = (ds_gc_t *)ds_arena_alloc_raw(a, sizeof(ds_gc_t));
    if (!g) return NULL;
    a->collector = g;
    a->collector_destroy = ds_gc_detach;
  }
  return g;
}

/* ------------------------------------------------------------------ */
/* Mark stack                                                          */
/* ------------------------------------------------------------------ */

void ds_gc_push_mark_stack_context(_ds_arena_t_ *a, ds_node_t node) {
  ds_type_t type;
  ds_gc_t *g;
  void *ptr;

  if (!a) return;

  type = ds_get_type(node);
  if (type != TYPE_NODE && type != TYPE_STRING) return;

  ptr = ds_get_ptr(node);
  if (!ptr) return;

  g = ds_gc_attach(a);
  if (!g) return;

  if (g->mark_stack_top >= g->mark_stack_cap) {
    size_t new_cap = g->mark_stack_cap ? g->mark_stack_cap * 2 : 1024;
    ds_node_t *new_stack = (ds_node_t *)realloc(g->mark_stack, sizeof(ds_node_t) * new_cap);
    if (!new_stack) {
      fputs("ds: out of memory (gc mark stack)\n", stderr);
      abort();
    }
    g->mark_stack = new_stack;
    g->mark_stack_cap = new_cap;
  }
  g->mark_stack[g->mark_stack_top++] = node;
}

/* ------------------------------------------------------------------ */
/* Roots                                                               */
/* ------------------------------------------------------------------ */

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
  _ds_gc_root_t_ *r;
  ds_gc_t *g;

  if (!a || !var_ptr) return;

  g = ds_gc_attach(a);
  if (!g) return;

  r = (_ds_gc_root_t_ *)ds_arena_alloc_internal(a, sizeof(_ds_gc_root_t_));
  r->variable_pointer = var_ptr;
  r->next = g->roots;
  g->roots = r;
}

void ds_gc_unregister_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
  _ds_gc_root_t_ **curr;
  ds_gc_t *g = ds_gc_state_of(a);

  if (!g || !var_ptr) return;

  curr = &g->roots;
  while (*curr) {
    if ((*curr)->variable_pointer == var_ptr) {
      *curr = (*curr)->next;
      return;
    }
    curr = &(*curr)->next;
  }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

/*
 * ds_arena_alloc stamps the header itself, so these two only exist to let a
 * caller move a block between the traced and untraced states after the fact.
 * The arena owns the transition -- it is the one keeping the live-block
 * accounting -- so both are one call deep and O(1).
 */

void ds_gc_register_allocation(_ds_arena_t_ *a, void *ptr, size_t size, const ds_type_descriptor_t *desc) {
  (void)size; /* the header is authoritative */
  ds_arena_block_promote(a, ptr, desc);
}

void ds_gc_unregister_allocation(_ds_arena_t_ *a, void *ptr) { ds_arena_block_demote(a, ptr); }

/* ------------------------------------------------------------------ */
/* Collection                                                          */
/* ------------------------------------------------------------------ */

/*
 * Sweeping runs in two separate passes over every chunk, and they cannot be
 * merged into one.
 *
 * A finalizer is allowed to touch the arena -- that is the whole point of
 * having one, and every container here uses it to hand back the raw payload
 * buffer it owns. Those buffers usually sit later in the same chunk as their
 * owner. Rebuilding the free list in the same walk that runs finalizers
 * therefore lets a finalizer push a block onto the list, after which the
 * walk reaches that same block, sees DS_BLOCK_FREE, and pushes it a second
 * time. Two links into one cell make the list cyclic, and the next
 * traversal never terminates.
 *
 * So: phase A finalizes and marks dead blocks free, touching no list. Phase
 * B is ds_arena_rebuild_free_lists, which derives every list from block
 * state alone -- so anything a finalizer recycled during phase A is simply
 * discovered by its state, exactly once. Phase B is arena code, because
 * which blocks are free is the collector's decision but how free space is
 * tracked is not.
 */

static void ds_gc_finalize_chunk(_ds_arena_t_ *a, _ds_arena_chunk_t_ *c) {
  char *p = c->payload;
  char *end = c->payload + c->chunk_size_used;

  while (p < end) {
    _ds_block_header_t_ *h = (_ds_block_header_t_ *)(void *)p;
    size_t step = h->total_size;

    if (step < DS_MIN_BLOCK || p + step > end) {
      fputs("ds: arena corruption detected during sweep\n", stderr);
      abort();
    }

    if (h->state == DS_BLOCK_MANAGED) {
      if (h->marked) {
        h->marked = 0;
      } else {
        /* The block is out of the live accounting before the finalizer runs,
         * so a finalizer that touches the arena cannot observe a half-dead
         * object. */
        void *payload = ds_payload_of(h);
        const ds_type_descriptor_t *desc = ds_arena_block_retire(a, h);

        if (desc && desc->finalize) desc->finalize(payload, a);
      }
    }

    p += step;
  }
}

void ds_gc_run(_ds_arena_t_ *a) {
  ds_gc_t *g;
  _ds_gc_root_t_ *root;
  _ds_arena_chunk_t_ *c;

  if (!a) return;

  g = ds_gc_state_of(a);

  /* ---- 1. Seed the work list from the registered roots ----
   *
   * With no collector state there are no roots, so nothing is reachable and
   * the mark phase has nothing to do -- but the sweep still runs, and
   * correctly collects every managed block. */
  if (g) {
    g->mark_stack_top = 0;
    for (root = g->roots; root; root = root->next) {
      if (root->variable_pointer) ds_gc_push_mark_stack_context(a, *(root->variable_pointer));
    }
  }

  /* ---- 2. Iterative mark ---- */
  while (g && g->mark_stack_top > 0) {
    ds_node_t node = g->mark_stack[--g->mark_stack_top];
    void *ptr = ds_get_ptr(node);
    _ds_block_header_t_ *h;

    if (!ptr) continue;

    h = ds_block_of(ptr);
    if (h->magic != DS_BLOCK_MAGIC) continue;   /* not an arena block */
    if (h->state != DS_BLOCK_MANAGED) continue; /* raw payload: nothing to trace */
    if (h->marked) continue;                    /* this is what breaks cycles */
    h->marked = 1;

    if (h->descriptor && h->descriptor->mark) h->descriptor->mark(node, a);
  }

  /* ---- 3. Sweep ---- */

  /* Drop every free list before finalizers run. A finalizer may allocate,
   * and it must not be handed a block out of a list that is about to be
   * rebuilt from scratch. Until phase B repopulates them, allocation falls
   * through to the bump pointer, which is always safe. */
  ds_arena_reset_free_lists(a);

  for (c = a->head; c; c = c->next_arena_chunk) ds_gc_finalize_chunk(a, c);

  ds_arena_rebuild_free_lists(a);
}

void ds_arena_run_gc(_ds_arena_t_ *a) { ds_gc_run(a); }
