#include "gc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"

// Structures de données internes du Garbage Collector
typedef struct _ds_gc_root_ {
  ds_node_t *variable_pointer;
  struct _ds_gc_root_ *next;
} _ds_gc_root_t_;

typedef struct _ds_allocation_track_ {
  void *ptr;
  int marked;
  struct _ds_allocation_track_ *next;
} _ds_allocation_track_t_;

// Variables statiques de suivi de l'état du GC
static _ds_gc_root_t_ *gc_roots = NULL;
static _ds_allocation_track_t_ *gc_allocs = NULL;
static ds_gc_mark_extension_func gc_custom_mark_callback = NULL;

// Configure l'extension de parcours externe pour les structures complexes (ex: graphes)
void ds_gc_set_mark_extension(ds_gc_mark_extension_func func) { gc_custom_mark_callback = func; }

// Enregistre une variable racine à protéger du balayage
void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
  _ds_gc_root_t_ *r = (_ds_gc_root_t_ *)ds_arena_alloc(a, sizeof(_ds_gc_root_t_));
  r->variable_pointer = var_ptr;
  r->next = gc_roots;
  gc_roots = r;
}

// Enregistre une allocation brute de l'arène pour suivi GC
void ds_gc_register_allocation(void *ptr) {
  _ds_allocation_track_t_ *track = malloc(sizeof(_ds_allocation_track_t_));
  track->ptr = ptr;
  track->marked = 0;
  track->next = gc_allocs;
  gc_allocs = track;
}

// Retire un pointeur du suivi actif lors d'un recyclage explicite (ex: pop/remove)
void ds_gc_unregister_allocation(void *ptr) {
  if (!ptr) return;
  _ds_allocation_track_t_ **curr = &gc_allocs;
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

// Phase Mark générique avec mécanisme de rupture de cycle infini
void gc_mark_node(ds_node_t node) {
  if (ds_get_type(node) != TYPE_NODE) return;
  void *ptr = ds_get_ptr(node);
  if (!ptr) return;

  _ds_allocation_track_t_ *curr = gc_allocs;
  while (curr) {
    if (curr->ptr == ptr) {
      if (curr->marked) return;  // Déjà marqué vivant, casse les boucles cycliques
      curr->marked = 1;
      break;
    }
    curr = curr->next;
  }

  // Délégation du marquage des structures internes si le callback est configuré
  if (gc_custom_mark_callback) {
    gc_custom_mark_callback(node);
  }
}

// Phase Sweep : Identifie les blocs morts et les renvoie à la Free-List externe de l'arène
void ds_arena_run_gc(_ds_arena_t_ *a) {
  _ds_gc_root_t_ *root = gc_roots;
  while (root) {
    if (root->variable_pointer) {
      gc_mark_node(*(root->variable_pointer));
    }
    root = root->next;
  }

  _ds_allocation_track_t_ **curr = &gc_allocs;
  int recycled_count = 0;

  while (*curr) {
    _ds_allocation_track_t_ *track = *curr;
    if (!track->marked) {
      // Sauvegarde de l'élément suivant avant la destruction du maillon par ds_arena_recycle
      _ds_allocation_track_t_ *next_track = track->next;

      ds_arena_recycle(a, track->ptr);

      *curr = next_track;
      recycled_count++;
    } else {
      track->marked = 0;  // Reset du drapeau pour le prochain cycle
      curr = &track->next;
    }
  }

  if (recycled_count > 0) {
    printf("[GC Externe] Nettoyage : %d bloc(s) de maillons recyclé(s) dans l'arène.\n", recycled_count);
  }
}

// Nettoie l'intégralité des structures de contrôle du tas à la fermeture du programme
void ds_gc_destroy(void) {
  _ds_allocation_track_t_ *curr_alloc = gc_allocs;
  while (curr_alloc) {
    _ds_allocation_track_t_ *tmp = curr_alloc->next;
    free(curr_alloc);
    curr_alloc = tmp;
  }
  gc_allocs = NULL;
  gc_roots = NULL;
  gc_custom_mark_callback = NULL;
}
