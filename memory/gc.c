#include "gc.h"

#include <stdio.h>
#include <stdlib.h>

#include "ds_arena.h"

static inline size_t ds_ptr_hash(void *ptr, size_t hash_size) {
  uintptr_t x = (uintptr_t)ptr;
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  return (size_t)(x & (hash_size - 1));
}

void ds_gc_push_mark_stack_context(_ds_arena_t_ *a, ds_node_t node) {
  if (ds_get_type(node) != TYPE_NODE || !ds_get_ptr(node)) return;

  if (a->gc_mark_stack_top >= a->gc_mark_stack_cap) {
    size_t new_cap = a->gc_mark_stack_cap ? a->gc_mark_stack_cap * 2 : 1024;
    ds_node_t *new_stack = realloc(a->gc_mark_stack, sizeof(ds_node_t) * new_cap);
    if (!new_stack) {
      fputs("ds: out of memory\n", stderr);
      abort();
    }
    a->gc_mark_stack = new_stack;
    a->gc_mark_stack_cap = new_cap;
  }
  a->gc_mark_stack[a->gc_mark_stack_top++] = node;
}

static void ds_gc_check_rehash(_ds_arena_t_ *a) {
  if ((float)a->gc_live_allocations / (float)a->gc_hash_size > 0.75f) {
    size_t old_size = a->gc_hash_size;
    size_t new_size = old_size * 2;

    _ds_allocation_track_t_ **new_buckets = calloc(new_size, sizeof(_ds_allocation_track_t_ *));
    if (!new_buckets) return;

    for (size_t i = 0; i < old_size; i++) {
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
}

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
  _ds_gc_root_t_ *r = (_ds_gc_root_t_ *)ds_arena_alloc_untracked(a, sizeof(_ds_gc_root_t_));
  r->variable_pointer = var_ptr;
  r->next = a->gc_roots;
  a->gc_roots = r;
}

void ds_gc_register_allocation(_ds_arena_t_ *a, void *ptr, size_t size) {
  ds_gc_check_rehash(a);

  size_t bucket = ds_ptr_hash(ptr, a->gc_hash_size);
  _ds_allocation_track_t_ *track = malloc(sizeof(_ds_allocation_track_t_));
  if (!track) {
    fputs("ds: out of memory\n", stderr);
    abort();
  }

  track->ptr = ptr;
  track->size = size;
  track->marked = 0;

  track->next = a->gc_buckets[bucket];
  a->gc_buckets[bucket] = track;

  a->gc_live_allocations++;
  a->gc_live_bytes += size;
  if (a->gc_live_bytes > a->gc_peak_live_bytes) a->gc_peak_live_bytes = a->gc_live_bytes;
}

void ds_gc_unregister_allocation(_ds_arena_t_ *a, void *ptr) {
  if (!a || !ptr || !a->gc_buckets) return;
  size_t bucket = ds_ptr_hash(ptr, a->gc_hash_size);

  _ds_allocation_track_t_ **curr = &a->gc_buckets[bucket];
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

void ds_gc_set_mark_extension(_ds_arena_t_ *a, ds_gc_mark_extension_func func) {
  if (a) a->gc_custom_mark_callback = func;
}

void ds_arena_run_gc(_ds_arena_t_ *a) {
  if (!a) return;

  a->gc_mark_stack_top = 0;
  if (!a->gc_mark_stack) {
    a->gc_mark_stack_cap = 1024;
    a->gc_mark_stack = malloc(sizeof(ds_node_t) * a->gc_mark_stack_cap);
    if (!a->gc_mark_stack) {
      fputs("ds: out of memory\n", stderr);
      abort();
    }
  }

  _ds_gc_root_t_ *root = a->gc_roots;
  while (root) {
    if (root->variable_pointer) {
      ds_gc_push_mark_stack_context(a, *(root->variable_pointer));
    }
    root = root->next;
  }

  while (a->gc_mark_stack_top > 0) {
    ds_node_t node = a->gc_mark_stack[--a->gc_mark_stack_top];
    void *ptr = ds_get_ptr(node);

    size_t bucket = ds_ptr_hash(ptr, a->gc_hash_size);
    _ds_allocation_track_t_ *track = a->gc_buckets[bucket];
    bool already_marked = false;

    while (track) {
      if (track->ptr == ptr) {
        if (track->marked) already_marked = true;
        track->marked = 1;
        break;
      }
      track = track->next;
    }

    if (already_marked) continue;

    if (a->gc_custom_mark_callback) {
      a->gc_custom_mark_callback(node, a);
    }
  }

  int recycled_count = 0;
  for (size_t i = 0; i < a->gc_hash_size; i++) {
    _ds_allocation_track_t_ **curr = &a->gc_buckets[i];
    while (*curr) {
      _ds_allocation_track_t_ *track = *curr;
      if (!track->marked) {
        _ds_allocation_track_t_ *next_track = track->next;

        ds_arena_recycle_raw(a, track->ptr, track->size);

        a->gc_live_allocations--;
        a->gc_live_bytes -= track->size;

        *curr = next_track;
        free(track);
        recycled_count++;
      } else {
        track->marked = 0;
        curr = &track->next;
      }
    }
  }

  if (recycled_count > 0) {
    printf("[GC Industriel] Nettoyage : %d bloc(s) recyclé(s) en O(1).\n", recycled_count);
  }
}
