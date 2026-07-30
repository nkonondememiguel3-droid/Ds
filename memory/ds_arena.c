#include "ds_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"

inline size_t ds_arena_align_up(size_t n) { return (n + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1); }

static inline void *_a_default_alloc(size_t bytes, void *context) {
  (void)context;
  return malloc(bytes);
}

static inline void *_a_default_realloc(void *ptr, size_t new_size, void *context) {
  (void)context;
  return realloc(ptr, new_size);
}

static inline void _a_default_free(void *ptr, void *context) {
  (void)context;
  free(ptr);
}

inline _ds_arena_t_ ds_arena_new(size_t chunk_size) {
  _ds_arena_t_ arena = (_ds_arena_t_){.head = NULL,
                                      .free_list_head = NULL,
                                      .chunk_size = chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE,
                                      .allocs_from_bump = 0,
                                      .allocs_from_free_list = 0};

  arena.imemory.alloc = _a_default_alloc;
  arena.imemory.realloc = _a_default_realloc;
  arena.imemory.free = _a_default_free;

  return arena;
}

static inline _ds_arena_chunk_t_ *ds_arena_grow(_ds_arena_t_ *a, size_t needed) {
  size_t sz = (a->chunk_size > needed) ? a->chunk_size : needed;
  sz = ds_arena_align_up(sz);
  _ds_arena_chunk_t_ *c = (_ds_arena_chunk_t_ *)a->imemory.alloc(sizeof(_ds_arena_chunk_t_) + sz, NULL);
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

inline void *ds_arena_alloc(_ds_arena_t_ *a, size_t size) {
  size = ds_arena_align_up(size);
  void *ptr = NULL;

  // 1. TENTATIVE DE RÉUTILISATION VIA LA FREE-LIST EXTERNE INDÉSTRUCTIBLE
  if (a->free_list_head != NULL) {
    _ds_free_block_t_ *node = a->free_list_head;

    // On récupère l'adresse physique préservée
    ptr = node->address;

    // On avance la tête vers le bloc libre suivant
    a->free_list_head = node->next;

    // On libère le petit maillon de contrôle externe
    free(node);

    // Nettoyage complet sécurisé du bloc réutilisé
    memset(ptr, 0, size);
    a->allocs_from_free_list++;
  }
  // 2. FALLBACK SUR LE BUMP POINTER
  else {
    _ds_arena_chunk_t_ *c = a->head;
    if (!c || c->chunk_size_used + size > c->chunk_size) c = ds_arena_grow(a, size);
    ptr = (void *)((char *)(c + 1) + c->chunk_size_used);
    c->chunk_size_used += size;
    memset(ptr, 0, size);

    a->allocs_from_bump++;
  }

  ds_gc_register_allocation(ptr);
  return ptr;
}

void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr) {
  if (!dead_ptr) return;

  ds_gc_unregister_allocation(dead_ptr);

  _ds_free_block_t_ *node = (_ds_free_block_t_ *)malloc(sizeof(_ds_free_block_t_));
  node->address = dead_ptr;

  node->next = a->free_list_head;
  a->free_list_head = node;
}

inline void ds_arena_destroy(_ds_arena_t_ *a) {
  _ds_arena_chunk_t_ *c = a->head;
  while (c) {
    _ds_arena_chunk_t_ *next = c->next_arena_chunk;
    a->imemory.free(c, NULL);
    c = next;
  }

  _ds_free_block_t_ *free_node = a->free_list_head;
  while (free_node) {
    _ds_free_block_t_ *tmp = free_node->next;
    free(free_node);
    free_node = tmp;
  }

  a->head = NULL;
  a->free_list_head = NULL;
}

inline _ds_arena_checkpoint_t_ ds_arena_checkpoint(_ds_arena_t_ *a) {
  return (_ds_arena_checkpoint_t_){.checkpoint_head = a->head,
                                   .checkpoint_size_used = a->head ? a->head->chunk_size_used : 0};
}

inline void ds_arena_reset_to(_ds_arena_t_ *a, _ds_arena_checkpoint_t_ cp) {
  while (a->head && a->head != cp.checkpoint_head) {
    _ds_arena_chunk_t_ *dead = a->head;
    a->head = dead->next_arena_chunk;
    a->imemory.free(dead, NULL);
  }
  if (a->head) a->head->chunk_size_used = cp.checkpoint_size_used;
  a->free_list_head = NULL;
}

_ds_arena_t_ ds_arena_new_with_allocator(size_t chunk_size, ds_mem_alloc_func m_alloc, ds_mem_realloc_func m_realloc,
                                         ds_mem_free_func m_free, void *context) {
  _ds_arena_t_ arena = (_ds_arena_t_){
      .head = NULL, .free_list_head = NULL, .chunk_size = chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE};
  arena.imemory.alloc = m_alloc;
  arena.imemory.realloc = m_realloc;
  arena.imemory.free = m_free;
  return arena;
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
