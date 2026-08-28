#include "gc.h"

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "ds_arena.h"

/*
 * Fibonacci/murmur-style pointer mixing.
 *
 * The original mixed in uintptr_t and shifted right by 33, which is
 * undefined behaviour on any 32-bit target (a shift count >= the width of
 * the type). Mixing in a fixed uint64_t makes the function well defined and
 * gives the same distribution on both word sizes.
 */
static DS_INLINE size_t ds_ptr_hash(const void *ptr, size_t hash_size) {
  uint64_t x = (uint64_t)(uintptr_t)ptr;
  x ^= x >> 33;
  x *= UINT64_C(0xff51afd7ed558ccd);
  x ^= x >> 33;
  x *= UINT64_C(0xc4ceb9fe1a85ec53);
  x ^= x >> 33;
  return (size_t)(x & (uint64_t)(hash_size - 1));
}

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

static void ds_gc_check_rehash(_ds_arena_t_ *a) {
  size_t old_size, new_size, i;
  _ds_allocation_track_t_ **new_buckets;

  if ((double)a->gc_live_allocations <= 0.75 * (double)a->gc_hash_size) return;

  old_size = a->gc_hash_size;
  new_size = old_size * 2;
  new_buckets = (_ds_allocation_track_t_ **)calloc(new_size, sizeof(_ds_allocation_track_t_ *));
  if (!new_buckets) return; /* stay at the current size rather than die */

  for (i = 0; i < old_size; i++) {
    _ds_allocation_track_t_ *curr = a->gc_buckets[i];
    while (curr) {
      _ds_allocation_track_t_ *next_node = curr->next;
      size_t new_bucket = ds_ptr_hash(curr->ptr, new_size);
      curr->next = new_buckets[new_bucket];
      new_buckets[new_bucket] = curr;
      curr = next_node;
    }
  }
  free(a->gc_buckets);
  a->gc_buckets = new_buckets;
  a->gc_hash_size = new_size;
}

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

void ds_gc_register_allocation(_ds_arena_t_ *a, void *ptr, size_t size, const ds_type_descriptor_t *desc) {
  size_t bucket;
  _ds_allocation_track_t_ *track;

  if (!a || !ptr) return;

  ds_gc_check_rehash(a);
  bucket = ds_ptr_hash(ptr, a->gc_hash_size);

  track = (_ds_allocation_track_t_ *)malloc(sizeof(_ds_allocation_track_t_));
  if (!track) {
    fputs("ds: out of memory (gc tracking record)\n", stderr);
    abort();
  }

  track->ptr = ptr;
  track->size = size;
  track->marked = 0;
  track->descriptor = desc;

  track->next = a->gc_buckets[bucket];
  a->gc_buckets[bucket] = track;
  a->gc_live_allocations++;
  a->gc_live_bytes += size;
  if (a->gc_live_bytes > a->gc_peak_live_bytes) a->gc_peak_live_bytes = a->gc_live_bytes;
}

void ds_gc_unregister_allocation(_ds_arena_t_ *a, void *ptr) {
  size_t bucket;
  _ds_allocation_track_t_ **curr;

  if (!a || !ptr || !a->gc_buckets) return;

  bucket = ds_ptr_hash(ptr, a->gc_hash_size);
  curr = &a->gc_buckets[bucket];
  while (*curr) {
    _ds_allocation_track_t_ *track = *curr;
    if (track->ptr == ptr) {
      *curr = track->next;
      a->gc_live_allocations--;
      a->gc_live_bytes -= track->size;
      free(track);
      return;
    }
    curr = &track->next;
  }
}

static _ds_allocation_track_t_ *ds_gc_lookup(_ds_arena_t_ *a, const void *ptr) {
  _ds_allocation_track_t_ *track = a->gc_buckets[ds_ptr_hash(ptr, a->gc_hash_size)];
  while (track) {
    if (track->ptr == ptr) return track;
    track = track->next;
  }
  return NULL;
}

void ds_arena_run_gc(_ds_arena_t_ *a) {
  _ds_gc_root_t_ *root;
  size_t i;

  /* The null check has to come before the arena is touched. The original
   * walked and printed the root list first and only then tested `a`. */
  if (!a || !a->gc_buckets) return;

  a->gc_mark_stack_top = 0;
  if (!a->gc_mark_stack) {
    a->gc_mark_stack_cap = 1024;
    a->gc_mark_stack = (ds_node_t *)malloc(sizeof(ds_node_t) * a->gc_mark_stack_cap);
    if (!a->gc_mark_stack) {
      fputs("ds: out of memory (gc mark stack)\n", stderr);
      abort();
    }
  }

  /* ---- 1. Seed the work list from the registered roots ---- */
  for (root = a->gc_roots; root; root = root->next) {
    if (root->variable_pointer) ds_gc_push_mark_stack_context(a, *(root->variable_pointer));
  }

  /* ---- 2. Iterative mark ---- */
  while (a->gc_mark_stack_top > 0) {
    ds_node_t node = a->gc_mark_stack[--a->gc_mark_stack_top];
    void *ptr = ds_get_ptr(node);
    _ds_allocation_track_t_ *track;

    if (!ptr) continue;

    track = ds_gc_lookup(a, ptr);
    if (!track) continue;        /* untracked (raw) memory: nothing to mark */
    if (track->marked) continue; /* already visited -- this is what breaks cycles */
    track->marked = 1;

    if (track->descriptor && track->descriptor->mark) track->descriptor->mark(node, a);
  }

  /* ---- 3. Sweep ---- */
  for (i = 0; i < a->gc_hash_size; i++) {
    _ds_allocation_track_t_ **curr = &a->gc_buckets[i];
    while (*curr) {
      _ds_allocation_track_t_ *track = *curr;
      if (!track->marked) {
        _ds_allocation_track_t_ *next_track = track->next;
        void *dead = track->ptr;
        size_t dead_size = track->size;
        const ds_type_descriptor_t *desc = track->descriptor;

        /* Unlink and account for the record *before* running the finalizer:
         * a finalizer is allowed to touch the arena (it typically recycles
         * the object's raw payload buffer) and must not be able to observe
         * or re-enter a half-removed tracking record. */
        *curr = next_track;
        a->gc_live_allocations--;
        a->gc_live_bytes -= dead_size;
        free(track);

        if (desc && desc->finalize) desc->finalize(dead, a);
        ds_arena_recycle_raw(a, dead, dead_size);
      } else {
        track->marked = 0;
        curr = &track->next;
      }
    }
  }
}
