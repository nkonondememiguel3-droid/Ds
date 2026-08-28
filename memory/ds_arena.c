#include "ds_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gc.h"

size_t ds_arena_align_up(size_t n) { return (n + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1); }

static void ds_oom(const char *what) {
  fputs("ds: out of memory (", stderr);
  fputs(what, stderr);
  fputs(")\n", stderr);
  abort();
}

_ds_arena_t_ *ds_arena_new(size_t chunk_size) {
  _ds_arena_t_ *a = (_ds_arena_t_ *)malloc(sizeof(_ds_arena_t_));
  if (!a) ds_oom("arena shell");
  memset(a, 0, sizeof(_ds_arena_t_));

  a->chunk_size = chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE;
  a->gc_hash_size = GC_HASH_SIZE;
  a->gc_buckets = (_ds_allocation_track_t_ **)calloc(a->gc_hash_size, sizeof(_ds_allocation_track_t_ *));
  if (!a->gc_buckets) ds_oom("gc bucket table");
  return a;
}

static _ds_arena_chunk_t_ *ds_arena_grow(_ds_arena_t_ *a, size_t needed) {
  size_t sz = (a->chunk_size > needed) ? a->chunk_size : needed;
  uintptr_t raw;
  _ds_arena_chunk_t_ *c;

  sz = ds_arena_align_up(sz);
  if (sz < needed) ds_oom("chunk size overflow"); /* align_up wrapped */

  /* Over-allocate by ARENA_ALIGN so the payload can be pushed forward to a
   * 16-byte boundary regardless of what malloc's own alignment happens to
   * be on this platform (8 on 32-bit, 16 on LP64, unspecified in theory). */
  if (sz > (size_t)-1 - sizeof(_ds_arena_chunk_t_) - ARENA_ALIGN) ds_oom("chunk size overflow");
  c = (_ds_arena_chunk_t_ *)malloc(sizeof(_ds_arena_chunk_t_) + ARENA_ALIGN + sz);
  if (!c) ds_oom("arena chunk");

  raw = (uintptr_t)((char *)c + sizeof(_ds_arena_chunk_t_));
  c->payload = (char *)((raw + (ARENA_ALIGN - 1)) & ~(uintptr_t)(ARENA_ALIGN - 1));

  c->chunk_size = sz;
  c->chunk_size_used = 0;
  c->free_list_head = NULL;
  c->next_arena_chunk = a->head;
  a->head = c;

  a->current_chunks++;
  if (a->current_chunks > a->peak_chunks) a->peak_chunks = a->current_chunks;
  return c;
}

static void *ds_arena_alloc_base(_ds_arena_t_ *a, size_t size) {
  _ds_arena_chunk_t_ *c;
  _ds_arena_chunk_t_ *chunk;
  void *ptr;
  size_t scanned = 0;

  if (size < ARENA_ALIGN) size = ARENA_ALIGN;
  size = ds_arena_align_up(size);

  /* ---- 1. Try to reuse a recycled block, with a bounded scan ---- */
  for (c = a->head; c && scanned < DS_FREELIST_SCAN_LIMIT; c = c->next_arena_chunk) {
    _ds_free_block_t_ *curr = c->free_list_head;
    _ds_free_block_t_ *prev = NULL;

    while (curr && scanned < DS_FREELIST_SCAN_LIMIT) {
      scanned++;
      if (curr->size >= size) {
        size_t taken = size;
        ptr = (void *)curr;

        /* Only split when the leftover is big enough to hold a free-list
         * cell of its own; otherwise hand out the whole block. */
        if (curr->size >= size + ARENA_ALIGN) {
          _ds_free_block_t_ *remainder = (_ds_free_block_t_ *)(void *)((char *)ptr + size);
          remainder->size = curr->size - size;
          remainder->next = curr->next;
          if (prev)
            prev->next = remainder;
          else
            c->free_list_head = remainder;
        } else {
          taken = curr->size;
          if (prev)
            prev->next = curr->next;
          else
            c->free_list_head = curr->next;
        }

        a->total_free_bytes_in_list -= taken;
        memset(ptr, 0, taken);
        a->allocs_from_free_list++;
        return ptr;
      }
      prev = curr;
      curr = curr->next;
    }
  }

  /* ---- 2. Bump-pointer path ---- */
  chunk = a->head;
  if (!chunk || size > chunk->chunk_size - chunk->chunk_size_used) chunk = ds_arena_grow(a, size);

  ptr = (void *)(chunk->payload + chunk->chunk_size_used);
  chunk->chunk_size_used += size;
  memset(ptr, 0, size);
  a->allocs_from_bump++;
  return ptr;
}

void *ds_arena_alloc_internal(_ds_arena_t_ *a, size_t size) { return ds_arena_alloc_base(a, size); }
void *ds_arena_alloc_raw(_ds_arena_t_ *a, size_t size) { return ds_arena_alloc_base(a, size); }

void *ds_arena_alloc(_ds_arena_t_ *a, size_t size, const ds_type_descriptor_t *desc) {
  void *ptr;
  if (size < ARENA_ALIGN) size = ARENA_ALIGN;
  size = ds_arena_align_up(size);
  ptr = ds_arena_alloc_base(a, size);
  ds_gc_register_allocation(a, ptr, size, desc);
  return ptr;
}

/* --- Local O(1) recycling with opportunistic coalescing --- */
void ds_arena_recycle_raw(_ds_arena_t_ *a, void *dead_ptr, size_t size) {
  _ds_arena_chunk_t_ *c;

  if (!a || !dead_ptr) return;

  /* Mirror the clamping done by the allocator. A block handed back with a
   * size below ARENA_ALIGN cannot hold the free-list cell that is about to
   * be written into it, which corrupted the neighbouring allocation. */
  if (size < ARENA_ALIGN) size = ARENA_ALIGN;
  size = ds_arena_align_up(size);

  for (c = a->head; c; c = c->next_arena_chunk) {
    char *chunk_start = c->payload;
    char *chunk_end = chunk_start + c->chunk_size;

    if ((char *)dead_ptr >= chunk_start && (char *)dead_ptr < chunk_end) {
      _ds_free_block_t_ *new_free;

      /* Never let a bad size argument push the cell past the chunk. */
      if ((size_t)(chunk_end - (char *)dead_ptr) < size) size = (size_t)(chunk_end - (char *)dead_ptr);
      if (size < ARENA_ALIGN) return;

      new_free = (_ds_free_block_t_ *)dead_ptr;
      new_free->size = size;
      new_free->next = c->free_list_head;
      c->free_list_head = new_free;

      a->total_free_bytes_in_list += size;

      /* If the block just inserted is physically adjacent to its successor
       * in the list, splice them together to undo the fragmentation. */
      if (new_free->next && (char *)new_free + new_free->size == (char *)new_free->next) {
        new_free->size += new_free->next->size;
        new_free->next = new_free->next->next;
      }
      return;
    }
  }
}

void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr, size_t size) {
  if (!a || !dead_ptr) return;
  ds_gc_unregister_allocation(a, dead_ptr);
  ds_arena_recycle_raw(a, dead_ptr, size);
}

void ds_arena_destroy(_ds_arena_t_ *a) {
  _ds_arena_chunk_t_ *c;

  if (!a) return;

  if (a->gc_buckets) {
    size_t i;
    for (i = 0; i < a->gc_hash_size; i++) {
      _ds_allocation_track_t_ *curr_alloc = a->gc_buckets[i];
      while (curr_alloc) {
        _ds_allocation_track_t_ *tmp = curr_alloc->next;
        free(curr_alloc);
        curr_alloc = tmp;
      }
    }
    free(a->gc_buckets);
    a->gc_buckets = NULL;
  }

  free(a->gc_mark_stack);
  a->gc_mark_stack = NULL;
  a->gc_roots = NULL; /* roots live inside the chunks freed just below */

  c = a->head;
  while (c) {
    _ds_arena_chunk_t_ *next = c->next_arena_chunk;
    free(c);
    c = next;
  }
  free(a);
}

void ds_arena_print_stats(const _ds_arena_t_ *a) {
  size_t total_demands;
  size_t total_arena_bytes = 0;
  size_t largest_free_block = 0;
  const _ds_arena_chunk_t_ *c;
  double frag_ratio;

  if (!a) return;

  total_demands = a->allocs_from_bump + a->allocs_from_free_list;

  for (c = a->head; c; c = c->next_arena_chunk) {
    const _ds_free_block_t_ *fb;
    total_arena_bytes += c->chunk_size;
    for (fb = c->free_list_head; fb; fb = fb->next) {
      if (fb->size > largest_free_block) largest_free_block = fb->size;
    }
  }

  frag_ratio = a->total_free_bytes_in_list
                   ? (1.0 - ((double)largest_free_block / (double)a->total_free_bytes_in_list)) * 100.0
                   : 0.0;

  printf("\n==================================================\n");
  printf("            ARENA TELEMETRY REPORT                \n");
  printf("==================================================\n");
  printf("Fresh allocations (bump pointer)    : %zu\n", a->allocs_from_bump);
  printf("Allocations served from free list   : %zu\n", a->allocs_from_free_list);
  printf("Total allocation requests           : %zu\n", total_demands);
  printf("--------------------------------------------------\n");
  printf("Live chunks                         : %zu\n", a->current_chunks);
  printf("Peak chunks                         : %zu\n", a->peak_chunks);
  printf("Total memory under management       : %zu bytes\n", total_arena_bytes);
  printf("Memory held in the free list        : %zu bytes\n", a->total_free_bytes_in_list);
  printf("Largest available free block        : %zu bytes\n", largest_free_block);
  printf("--------------------------------------------------\n");
  printf("Live GC allocations                 : %zu\n", a->gc_live_allocations);
  printf("Live GC bytes                       : %zu bytes\n", a->gc_live_bytes);
  printf("Peak live GC bytes                  : %zu bytes\n", a->gc_peak_live_bytes);
  printf("External fragmentation ratio        : %.2f%%\n", frag_ratio);
  printf("==================================================\n\n");
}
