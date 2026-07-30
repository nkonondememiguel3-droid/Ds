#include "gc.h"
#include "ds_arena.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct _ds_gc_root_ {
    ds_node_t *variable_pointer;
    struct _ds_gc_root_ *next;
} _ds_gc_root_t_;

typedef struct _ds_allocation_track_ {
    void *ptr;
    int marked;
    struct _ds_allocation_track_ *next;
} _ds_allocation_track_t_;

static _ds_gc_root_t_ *gc_roots = NULL;
static _ds_allocation_track_t_ *gc_allocs = NULL;

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr) {
    _ds_gc_root_t_ *r = (_ds_gc_root_t_*)ds_arena_alloc(a, sizeof(_ds_gc_root_t_));
    r->variable_pointer = var_ptr;
    r->next = gc_roots;
    gc_roots = r;
}

void ds_gc_register_allocation(void *ptr) {
    _ds_allocation_track_t_ *track = malloc(sizeof(_ds_allocation_track_t_));
    track->ptr = ptr;
    track->marked = 0;
    track->next = gc_allocs;
    gc_allocs = track;
}

static void gc_mark_node(ds_node_t node) {
    if (ds_get_type(node) != TYPE_NODE) return;
    void *ptr = ds_get_ptr(node);
    if (!ptr) return;

    _ds_allocation_track_t_ *curr = gc_allocs;
    while (curr) {
        if (curr->ptr == ptr) {
            if (curr->marked) return;
            curr->marked = 1;
            break;
        }
        curr = curr->next;
    }

    // Parcours récursif générique : On considère que vos structures de données (ex: nœuds d'arbres ou de listes)
    // commencent par des champs de pointeurs étiquetés ds_node_t (comme des paires Lisp car/cdr)
    ds_node_t *sub_fields = (ds_node_t*)ptr;
    gc_mark_node(sub_fields[0]); // Champ 1
    gc_mark_node(sub_fields[1]); // Champ 2
}

void ds_arena_run_gc(_ds_arena_t_ *a) {
    // 1. PHASE MARK
    _ds_gc_root_t_ *root = gc_roots;
    while (root) {
        if (root->variable_pointer) {
            gc_mark_node(*(root->variable_pointer));
        }
        root = root->next;
    }

    // 2. PHASE SWEEP
    _ds_allocation_track_t_ **curr = &gc_allocs;
    int recycled_count = 0;

    while (*curr) {
        _ds_allocation_track_t_ *track = *curr;
        if (!track->marked) {
            // L'objet est mort ! On le renvoie à la free-list interne de l'arène
            ds_arena_recycle(a, track->ptr);

            *curr = track->next;
            free(track);
            recycled_count++;
        } else {
            track->marked = 0; // Reset pour le prochain cycle
            curr = &track->next;
        }
    }

    if (recycled_count > 0) {
        printf("[GC Externe] Nettoyage : %d bloc(s) de maillons recyclé(s) dans l'arène.\n", recycled_count);
    }
}

void ds_gc_destroy(void) {
    _ds_allocation_track_t_ *curr_alloc = gc_allocs;
    while (curr_alloc) {
        _ds_allocation_track_t_ *tmp = curr_alloc->next;
        free(curr_alloc);
        curr_alloc = tmp;
    }
    gc_allocs = NULL;
    gc_roots = NULL; // Les structures de racines étaient dans l'arène, détruites par ds_arena_destroy
}
