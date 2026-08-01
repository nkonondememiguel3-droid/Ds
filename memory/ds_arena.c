#include "ds_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"

inline size_t ds_arena_align_up(size_t n) { return (n + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1); }

_ds_arena_t_ ds_arena_new(size_t chunk_size) {
  return (_ds_arena_t_){.head = NULL,
                        .free_list_head = NULL,
                        .chunk_size = chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE,
                        .gc_roots = NULL,
                        .gc_allocs = NULL,
                        .gc_custom_mark_callback = NULL,
                        .allocs_from_bump = 0,
                        .allocs_from_free_list = 0};
}

static inline _ds_arena_chunk_t_ *ds_arena_grow(_ds_arena_t_ *a, size_t needed) {
  size_t sz = (a->chunk_size > needed) ? a->chunk_size : needed;
  sz = ds_arena_align_up(sz);

  // Allocation du header (32 octets) + charge utile alignée
  _ds_arena_chunk_t_ *c = (_ds_arena_chunk_t_ *)malloc(sizeof(_ds_arena_chunk_t_) + sz);
  if (!c) {
    fputs("ds: out of memory in arena_grow\n", stderr);
    abort();
  }
  c->chunk_size = sz;
  c->chunk_size_used = 0;
  c->next_arena_chunk = a->head;
  a->head = c;
  return c;
}

void *ds_arena_alloc_raw(_ds_arena_t_ *a, size_t size) {
  if (size < ARENA_ALIGN) size = ARENA_ALIGN;
  size = ds_arena_align_up(size);
  void *ptr = NULL;

  // Algorithme de recherche First-Fit
  _ds_free_block_t_ *curr = a->free_list_head;
  _ds_free_block_t_ *prev = NULL;

  while (curr) {
    if (curr->size >= size) {
      ptr = curr->address;

      // Extraction de la Free-list
      if (prev) {
        prev->next = curr->next;
      } else {
        a->free_list_head = curr->next;
      }

      // OPTIMISATION : Fragmentation - Si le bloc extrait est au moins deux fois plus grand
      // que nécessaire, on le sépare (Block Splitting) pour éviter le gaspillage
      if (curr->size >= size + ARENA_ALIGN) {
        _ds_free_block_t_ *remainder = (_ds_free_block_t_ *)malloc(sizeof(_ds_free_block_t_));
        if (remainder) {
          remainder->address = (char *)ptr + size;
          remainder->size = curr->size - size;
          remainder->next = a->free_list_head;
          a->free_list_head = remainder;
        }
      }

      free(curr);
      memset(ptr, 0, size);
      a->allocs_from_free_list++;
      return ptr;
    }
    prev = curr;
    curr = curr->next;
  }

  // Séquentiel (Bump) : (chunk + 1) est désormais rigoureusement aligné sur 16 et 32 octets
  _ds_arena_chunk_t_ *chunk = a->head;
  if (!chunk || chunk->chunk_size_used + size > chunk->chunk_size) {
    chunk = ds_arena_grow(a, size);
  }
  ptr = (void *)((char *)(chunk + 1) + chunk->chunk_size_used);
  chunk->chunk_size_used += size;

  memset(ptr, 0, size);
  a->allocs_from_bump++;
  return ptr;
}

void *ds_arena_alloc(_ds_arena_t_ *a, size_t size) {
  if (size < ARENA_ALIGN) size = ARENA_ALIGN;
  size = ds_arena_align_up(size);

  void *ptr = ds_arena_alloc_raw(a, size);

  // --- CORRECTIF : TRANSMISSION DE LA TAILLE CALCULÉE AU TRACKER DU GC ---
  ds_gc_register_allocation(a, ptr, size);
  return ptr;
}

void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr, size_t size) {
  if (!dead_ptr) return;
  size = ds_arena_align_up(size);

  ds_gc_unregister_allocation(a, dead_ptr);

  _ds_free_block_t_ *node = (_ds_free_block_t_ *)malloc(sizeof(_ds_free_block_t_));
  if (!node) {
    fputs("ds: out of memory in arena_recycle tracker\n", stderr);
    abort();
  }
  node->address = dead_ptr;
  node->size = size;
  node->next = a->free_list_head;
  a->free_list_head = node;
}

void ds_arena_destroy(_ds_arena_t_ *a) {
  _ds_arena_chunk_t_ *c = a->head;
  while (c) {
    _ds_arena_chunk_t_ *next = c->next_arena_chunk;
    free(c);
    c = next;
  }

  _ds_free_block_t_ *free_node = a->free_list_head;
  while (free_node) {
    _ds_free_block_t_ *tmp = free_node->next;
    free(free_node);
    free_node = tmp;
  }

  _ds_allocation_track_t_ *curr_alloc = a->gc_allocs;
  while (curr_alloc) {
    _ds_allocation_track_t_ *tmp = curr_alloc->next;
    free(curr_alloc);
    curr_alloc = tmp;
  }

  a->head = NULL;
  a->free_list_head = NULL;
  a->gc_allocs = NULL;
  a->gc_roots = NULL;
}

void ds_arena_print_stats(const _ds_arena_t_ *a) {
  if (!a) return;
  size_t total = a->allocs_from_bump + a->allocs_from_free_list;
  float ratio = total ? ((float)a->allocs_from_free_list / (float)total) * 100.0f : 0.0f;

  printf("\n==================================================\n");
  printf("         RAPPORT DE TÉLÉMÉTRIE DE L'ARÈNE         \n");
  printf("==================================================\n");
  printf("Allocations neuves (Bump Pointer)   : %zu\n", a->allocs_from_bump);
  printf("Allocations recyclées (Free-List)   : %zu\n", a->allocs_from_free_list);
  printf("Total des demandes de mémoire       : %zu\n", total);
  printf("Taux d'efficacité du recyclage GC   : %.2f%%\n", ratio);
  printf("==================================================\n\n");
}
