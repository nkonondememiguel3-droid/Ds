#include "gc.h"

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "ds_arena.h"

/*
 * Mark and sweep over inline block headers.
 *
 * There is no side table: every block the arena hands out carries its own
 * descriptor, size and mark bit in the ARENA_ALIGN bytes immediately before
 * the payload, and every byte of a chunk's used region belongs to exactly
 * one block. Marking is therefore a pointer subtraction, and sweeping is a
 * linear walk of each chunk in address order.
 */

void ds_gc_push_mark_stack_context(_ds_arena_t_ *a, ds_node_t node) {
  ds_type_t type;
  void *ptr;

  if (!a) return;

  type = ds_get_type(node);
  if (type != TYPE_NODE && type != TYPE_STRING) return;

  ptr = ds_get_ptr(node);
  if (!ptr) return;

  if (a->gc_mark_stack_top >= a->gc_mark_stack_cap) {
    size_t new_cap = a->gc_mark_stack_cap ? a->gc_mark_stack_cap * 2 : 1024;
    ds_node_t *new_stack = (ds_node_t *)realloc(a->gc_mark_stack, sizeof(ds_node_t) * new_cap);
    if (!new_stack) {
      fputs("ds: out of memory (gc mark stack)\n", stderr);
      abort();
    }
    a->gc_mark_stack = new_stack;
    a->gc_mark_stack_cap = new_cap;
  }
  a->gc_mark_stack[a->gc_mark_stack_top++] = node;
}

/* ------------------------------------------------------------------ */
/* Roots                                                               */
/* ------------------------------------------------------------------ */

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
  _ds_gc_root_t_ *r;
  if (!a || !var_ptr) return;

  r = (_ds_gc_root_t_ *)ds_arena_alloc_internal(a, sizeof(_ds_gc_root_t_));
  r->variable_pointer = var_ptr;
  r->next = a->gc_roots;
  a->gc_roots = r;
}

void ds_gc_unregister_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
  _ds_gc_root_t_ **curr;
  if (!a || !var_ptr) return;

  curr = &a->gc_roots;
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
 * ds_arena_alloc now stamps the header itself, so these two only exist to
 * let a caller move a block between the traced and untraced states after
 * the fact. Both are O(1) with no allocation of their own.
 */

void ds_gc_register_allocation(_ds_arena_t_ *a, void *ptr, size_t size, const ds_type_descriptor_t *desc) {
  _ds_block_header_t_ *h;

  (void)size; /* the header is authoritative */
  if (!a || !ptr) return;

  h = ds_block_of(ptr);
  if (h->magic != DS_BLOCK_MAGIC || h->state == DS_BLOCK_MANAGED) return;

  h->state = DS_BLOCK_MANAGED;
  h->descriptor = desc;
  h->marked = 0;
  a->gc_live_allocations++;
  a->gc_live_bytes += h->total_size;
  if (a->gc_live_bytes > a->gc_peak_live_bytes) a->gc_peak_live_bytes = a->gc_live_bytes;
}

void ds_gc_unregister_allocation(_ds_arena_t_ *a, void *ptr) {
  _ds_block_header_t_ *h;

  if (!a || !ptr) return;

  h = ds_block_of(ptr);
  if (h->magic != DS_BLOCK_MAGIC || h->state != DS_BLOCK_MANAGED) return;

  h->state = DS_BLOCK_RAW;
  h->descriptor = NULL;
  h->marked = 0;
  a->gc_live_allocations--;
  a->gc_live_bytes -= h->total_size;
}

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
 * So: phase A finalizes and marks dead blocks free, touching no list.
 * Phase B rebuilds every list from the block states alone. Anything a
 * finalizer recycled in phase A is simply discovered in phase B by its
 * state, exactly once.
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
        const ds_type_descriptor_t *desc = h->descriptor;
        void *payload = ds_payload_of(h);

        a->gc_live_allocations--;
        a->gc_live_bytes -= h->total_size;
        h->state = DS_BLOCK_FREE;
        h->descriptor = NULL;

        /* The block is out of the live accounting before the finalizer
         * runs, so a finalizer that touches the arena cannot observe a
         * half-dead object. */
        if (desc && desc->finalize) desc->finalize(payload, a);
      }
    }

    p += step;
  }
}

static void ds_gc_rebuild_chunk(_ds_arena_t_ *a, _ds_arena_chunk_t_ *c) {
  char *p = c->payload;
  char *end = c->payload + c->chunk_size_used;
  _ds_block_header_t_ *run = NULL; /* open run of adjacent free blocks */

  c->free_list_head = NULL;

  /* The walk visits blocks in address order, so neighbours are consecutive
   * iterations and merging them is a comparison away. The previous
   * head-insert scheme could only ever merge a block with whatever happened
   * to be at the head of the list, and the old sweep visited objects in
   * pointer-hash order, so in practice it merged nothing. */
  while (p < end) {
    _ds_block_header_t_ *h = (_ds_block_header_t_ *)(void *)p;
    size_t step = h->total_size;

    if (step < DS_MIN_BLOCK || p + step > end) {
      fputs("ds: arena corruption detected during sweep\n", stderr);
      abort();
    }

    if (h->state == DS_BLOCK_FREE) {
      if (run && (char *)run + run->total_size == p) {
        run->total_size += h->total_size; /* absorb into the open run */
      } else {
        _ds_free_cell_t_ *cell = (_ds_free_cell_t_ *)ds_payload_of(h);
        cell->next = c->free_list_head;
        c->free_list_head = cell;
        run = h;
      }
    } else {
      run = NULL;
    }

    p += step;
  }

  /* A merged run's size grew after it was linked, so the byte total has to
   * be taken from the finished list rather than accumulated above. */
  {
    const _ds_free_cell_t_ *fc;
    size_t chunk_free = 0;
    for (fc = c->free_list_head; fc; fc = fc->next) {
      chunk_free += (((const _ds_block_header_t_ *)fc) - 1)->total_size;
    }
    a->total_free_bytes_in_list += chunk_free;
  }

  if (c->free_list_head) {
    c->on_free_ring = 1;
    c->next_free_chunk = a->free_chunks;
    a->free_chunks = c;
  }
}

void ds_arena_run_gc(_ds_arena_t_ *a) {
  _ds_gc_root_t_ *root;
  _ds_arena_chunk_t_ *c;

  if (!a) return;

  a->gc_mark_stack_top = 0;

  /* ---- 1. Seed the work list from the registered roots ---- */
  for (root = a->gc_roots; root; root = root->next) {
    if (root->variable_pointer) ds_gc_push_mark_stack_context(a, *(root->variable_pointer));
  }

  /* ---- 2. Iterative mark ---- */
  while (a->gc_mark_stack_top > 0) {
    ds_node_t node = a->gc_mark_stack[--a->gc_mark_stack_top];
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
  for (c = a->head; c; c = c->next_arena_chunk) {
    c->free_list_head = NULL;
    c->on_free_ring = 0;
    c->next_free_chunk = NULL;
  }
  a->free_chunks = NULL;
  a->total_free_bytes_in_list = 0;

  for (c = a->head; c; c = c->next_arena_chunk) ds_gc_finalize_chunk(a, c);

  /* Phase B discards whatever the finalizers linked and derives the free
   * lists from block state alone, so every free block is linked exactly
   * once however the finalizers behaved. */
  for (c = a->head; c; c = c->next_arena_chunk) {
    c->free_list_head = NULL;
    c->on_free_ring = 0;
    c->next_free_chunk = NULL;
  }
  a->free_chunks = NULL;
  a->total_free_bytes_in_list = 0;

  for (c = a->head; c; c = c->next_arena_chunk) ds_gc_rebuild_chunk(a, c);
}
