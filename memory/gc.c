#include "gc.h"

#include <stdio.h>
#include <stdlib.h>

#include "ds_arena.h"

void ds_gc_set_mark_extension(_ds_arena_t_ *a, ds_gc_mark_extension_func func) {
  if (a) a->gc_custom_mark_callback = func;
}

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
  _ds_gc_root_t_ *r = (_ds_gc_root_t_ *)ds_arena_alloc_raw(a, sizeof(_ds_gc_root_t_));
  r->variable_pointer = var_ptr;
  r->next = a->gc_roots;
  a->gc_roots = r;
}

void ds_gc_register_allocation(_ds_arena_t_ *a, void *ptr, size_t size) {
  _ds_allocation_track_t_ *track = malloc(sizeof(_ds_allocation_track_t_));
  if (!track) {
    fputs("ds: out of memory in GC tracking block\n", stderr);
    abort();
  }
  track->ptr = ptr;
  track->size = size;  // O(1) : Enregistrement de la taille réelle calculée par l'arène
  track->marked = 0;
  track->next = a->gc_allocs;
  a->gc_allocs = track;
}

void ds_gc_unregister_allocation(_ds_arena_t_ *a, void *ptr) {
  if (!a || !ptr) return;
  _ds_allocation_track_t_ **curr = &a->gc_allocs;
  while (*curr) {
    _ds_allocation_track_t_ *track = *curr;
    if (track->ptr == ptr) {
      *curr = track->next;
      free(track);
      return;
    }
    curr = &track->next;
  }
}

static void gc_mark_node(_ds_arena_t_ *a, ds_node_t node) {
  if (ds_get_type(node) != TYPE_NODE) return;
  void *ptr = ds_get_ptr(node);
  if (!ptr) return;

  _ds_allocation_track_t_ *curr = a->gc_allocs;
  while (curr) {
    if (curr->ptr == ptr) {
      if (curr->marked) return;
      curr->marked = 1;
      break;
    }
    curr = curr->next;
  }

  if (a->gc_custom_mark_callback) {
    a->gc_custom_mark_callback(node);
  }
}

void ds_arena_run_gc(_ds_arena_t_ *a) {
  if (!a) return;

  _ds_gc_root_t_ *root = a->gc_roots;
  while (root) {
    if (root->variable_pointer) {
      gc_mark_node(a, *(root->variable_pointer));
    }
    root = root->next;
  }

  _ds_allocation_track_t_ **curr = &a->gc_allocs;
  int recycled_count = 0;

  while (*curr) {
    _ds_allocation_track_t_ *track = *curr;
    if (!track->marked) {
      _ds_allocation_track_t_ *next_track = track->next;

      // --- INCROYABLEMENT SÛR : LA VRAIE TAILLE COMPLÈTE DU BLOC EST RESTITUÉE ---
      ds_arena_recycle(a, track->ptr, track->size);

      *curr = next_track;
      recycled_count++;
    } else {
      track->marked = 0;
      curr = &track->next;
    }
  }

  if (recycled_count > 0) {
    printf("[GC Contexte] Nettoyage : %d bloc(s) de taille variable recyclé(s) fidèlement.\n", recycled_count);
  }
}
