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
                        .allocs_from_free_list = 0,
                        .peak_chunks = 0,
                        .current_chunks = 0,
                        .total_free_bytes_in_list = 0};
}

static inline _ds_arena_chunk_t_ *ds_arena_grow(_ds_arena_t_ *a, size_t needed) {
  size_t sz = (a->chunk_size > needed) ? a->chunk_size : needed;
  sz = ds_arena_align_up(sz);

  _ds_arena_chunk_t_ *c = (_ds_arena_chunk_t_ *)malloc(sizeof(_ds_arena_chunk_t_) + sz);
  if (!c) {
    fputs("ds: out of memory\n", stderr);
    abort();
  }

  c->chunk_size = sz;
  c->chunk_size_used = 0;
  c->next_arena_chunk = a->head;
  a->head = c;

  a->current_chunks++;
  if (a->current_chunks > a->peak_chunks) {
    a->peak_chunks = a->current_chunks;
  }
  return c;
}

void *ds_arena_alloc_raw(_ds_arena_t_ *a, size_t size) {
  if (size < ARENA_ALIGN) size = ARENA_ALIGN;
  size = ds_arena_align_up(size);

  _ds_free_block_t_ *curr = a->free_list_head;
  _ds_free_block_t_ *prev = NULL;

  while (curr) {
    if (curr->size >= size) {
      void *ptr = (void *)curr;

      if (curr->size >= size + sizeof(_ds_free_block_t_)) {
        _ds_free_block_t_ *remainder = (_ds_free_block_t_ *)((char *)ptr + size);
        remainder->size = curr->size - size;
        remainder->next = curr->next;

        if (prev) {
          prev->next = remainder;
        } else {
          a->free_list_head = remainder;
        }
      } else {
        size = curr->size;
        if (prev) {
          prev->next = curr->next;
        } else {
          a->free_list_head = curr->next;
        }
      }

      a->total_free_bytes_in_list -= size;
      memset(ptr, 0, size);
      a->allocs_from_free_list++;
      return ptr;
    }
    prev = curr;
    curr = curr->next;
  }

  _ds_arena_chunk_t_ *chunk = a->head;
  if (!chunk || chunk->chunk_size_used + size > chunk->chunk_size) {
    chunk = ds_arena_grow(a, size);
  }
  void *ptr = (void *)((char *)(chunk + 1) + chunk->chunk_size_used);
  chunk->chunk_size_used += size;

  memset(ptr, 0, size);
  a->allocs_from_bump++;
  return ptr;
}

void *ds_arena_alloc(_ds_arena_t_ *a, size_t size) {
  if (size < ARENA_ALIGN) size = ARENA_ALIGN;
  size = ds_arena_align_up(size);
  void *ptr = ds_arena_alloc_raw(a, size);
  ds_gc_register_allocation(a, ptr, size);
  return ptr;
}

void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr, size_t size) {
  if (!dead_ptr) return;
  size = ds_arena_align_up(size);

  ds_gc_unregister_allocation(a, dead_ptr);

  // --- FAUTE DE FRAPPE RECTIFIÉE : UTILISATION STRICTE DE _ds_free_block_t_ ---
  _ds_free_block_t_ *new_free = (_ds_free_block_t_ *)dead_ptr;
  new_free->size = size;

  _ds_free_block_t_ *curr = a->free_list_head;
  _ds_free_block_t_ *prev = NULL;

  while (curr && (void *)curr < dead_ptr) {
    prev = curr;
    curr = curr->next;
  }

  if (prev) {
    new_free->next = prev->next;
    prev->next = new_free;
  } else {
    new_free->next = a->free_list_head;
    a->free_list_head = new_free;
  }
  a->total_free_bytes_in_list += size;

  // Algorithme de Coalescing (Fusion)
  if (new_free->next && (char *)new_free + new_free->size == (char *)new_free->next) {
    new_free->size += new_free->next->size;
    new_free->next = new_free->next->next;
  }
  if (prev && (char *)prev + prev->size == (char *)new_free) {
    prev->size += new_free->size;
    prev->next = new_free->next;
  }
}

void ds_arena_destroy(_ds_arena_t_ *a) {
  _ds_arena_chunk_t_ *c = a->head;
  while (c) {
    _ds_arena_chunk_t_ *next = c->next_arena_chunk;
    free(c);
    c = next;
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
  a->current_chunks = 0;
  a->total_free_bytes_in_list = 0;
}

void ds_arena_print_stats(const _ds_arena_t_ *a) {
  if (!a) return;
  size_t total_demands = a->allocs_from_bump + a->allocs_from_free_list;

  size_t total_arena_bytes = 0;
  _ds_arena_chunk_t_ *c = a->head;
  while (c) {
    total_arena_bytes += c->chunk_size;
    c = c->next_arena_chunk;
  }

  float frag_ratio =
      total_arena_bytes ? ((float)a->total_free_bytes_in_list / (float)total_arena_bytes) * 100.0f : 0.0f;

  printf("\n==================================================\n");
  printf("     RAPPORT DE TÉLÉMÉTRIE AVANCÉ DE L'ARÈNE      \n");
  printf("==================================================\n");
  printf("Allocations neuves (Bump Pointer)   : %zu\n", a->allocs_from_bump);
  printf("Allocations recyclées (Free-List)   : %zu\n", a->allocs_from_free_list);
  printf("Total des demandes de mémoire       : %zu\n", total_demands);
  printf("--------------------------------------------------\n");
  printf("Chunks actifs au système (Current)  : %zu\n", a->current_chunks);
  printf("Pic Historique de Chunks (Peak)     : %zu\n", a->peak_chunks);
  printf("Volume Mémoire total géré (Bytes)   : %zu\n", total_arena_bytes);
  printf("Mémoire Libre dans la Free-List     : %zu bytes\n", a->total_free_bytes_in_list);
  printf("Ratio de Fragmentation Externe      : %.2f%%\n", frag_ratio);
  printf("==================================================\n\n");
}
