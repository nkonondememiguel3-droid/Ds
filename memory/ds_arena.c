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
  return a;
}

/* ------------------------------------------------------------------ */
/* Chunks                                                              */
/* ------------------------------------------------------------------ */

static _ds_arena_chunk_t_ *ds_arena_grow(_ds_arena_t_ *a, size_t needed) {
  size_t sz = (a->chunk_size > needed) ? a->chunk_size : needed;
  uintptr_t raw;
  _ds_arena_chunk_t_ *c;

  sz = ds_arena_align_up(sz);
  if (sz < needed) ds_oom("chunk size overflow");

  /* Over-allocate by ARENA_ALIGN so the payload can be pushed forward to a
   * 16-byte boundary regardless of malloc's own alignment on this platform. */
  if (sz > (size_t)-1 - sizeof(_ds_arena_chunk_t_) - ARENA_ALIGN) ds_oom("chunk size overflow");
  c = (_ds_arena_chunk_t_ *)malloc(sizeof(_ds_arena_chunk_t_) + ARENA_ALIGN + sz);
  if (!c) ds_oom("arena chunk");

  raw = (uintptr_t)((char *)c + sizeof(_ds_arena_chunk_t_));
  c->payload = (char *)((raw + (ARENA_ALIGN - 1)) & ~(uintptr_t)(ARENA_ALIGN - 1));

  c->chunk_size = sz;
  c->chunk_size_used = 0;
  c->free_list_head = NULL;
  c->next_free_chunk = NULL;
  c->on_free_ring = 0;
  c->next_arena_chunk = a->head;
  a->head = c;

  a->current_chunks++;
  if (a->current_chunks > a->peak_chunks) a->peak_chunks = a->current_chunks;
  return c;
}

/* Put a chunk on the free ring the first time it acquires a free block. */
static void ds_chunk_mark_free(_ds_arena_t_ *a, _ds_arena_chunk_t_ *c) {
  if (c->on_free_ring) return;
  c->on_free_ring = 1;
  c->next_free_chunk = a->free_chunks;
  a->free_chunks = c;
}

static _ds_arena_chunk_t_ *ds_chunk_owning(_ds_arena_t_ *a, const void *p) {
  _ds_arena_chunk_t_ *c;
  for (c = a->head; c; c = c->next_arena_chunk) {
    if ((const char *)p >= c->payload && (const char *)p < c->payload + c->chunk_size) return c;
  }
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Allocation                                                          */
/* ------------------------------------------------------------------ */

static void ds_block_init(_ds_block_header_t_ *h, size_t total, const ds_type_descriptor_t *desc, uint8_t state) {
  h->descriptor = desc;
  h->total_size = (uint32_t)total;
  h->magic = DS_BLOCK_MAGIC;
  h->state = state;
  h->marked = 0;
}

static void ds_account_live(_ds_arena_t_ *a, size_t total) {
  a->gc_live_allocations++;
  a->gc_live_bytes += total;
  if (a->gc_live_bytes > a->gc_peak_live_bytes) a->gc_peak_live_bytes = a->gc_live_bytes;
}

static void *ds_arena_alloc_block(_ds_arena_t_ *a, size_t payload, const ds_type_descriptor_t *desc, uint8_t state) {
  size_t total;
  _ds_arena_chunk_t_ *c;
  _ds_arena_chunk_t_ *prev_chunk;
  _ds_arena_chunk_t_ *chunk;
  _ds_block_header_t_ *h;
  size_t scanned = 0;

  if (!a) return NULL;

  if (payload < ARENA_ALIGN) payload = ARENA_ALIGN;
  payload = ds_arena_align_up(payload);
  if (payload > (size_t)0xFFFFFFFFu - sizeof(_ds_block_header_t_)) ds_oom("allocation too large");
  total = payload + sizeof(_ds_block_header_t_);

  /* ---- 1. Reuse a recycled block, with a bounded scan ----
   *
   * Only chunks that actually hold free blocks are visited. Walking the
   * full chunk list here made allocation O(number of chunks) whenever the
   * free lists were empty -- the per-cell budget below never advanced, so
   * it did not bound anything -- which turned a workload that grows many
   * chunks into a quadratic one. */
  prev_chunk = NULL;
  c = a->free_chunks;
  while (c && scanned < DS_FREELIST_SCAN_LIMIT) {
    _ds_free_cell_t_ *curr = c->free_list_head;
    _ds_free_cell_t_ *prev = NULL;

    if (!curr) { /* drained since it was threaded on: unlink lazily */
      _ds_arena_chunk_t_ *next_c = c->next_free_chunk;
      c->on_free_ring = 0;
      c->next_free_chunk = NULL;
      if (prev_chunk)
        prev_chunk->next_free_chunk = next_c;
      else
        a->free_chunks = next_c;
      c = next_c;
      continue;
    }

    while (curr && scanned < DS_FREELIST_SCAN_LIMIT) {
      _ds_block_header_t_ *ch = ds_block_of(curr);
      scanned++;

      if (ch->total_size >= total) {
        size_t block_total = ch->total_size;
        _ds_free_cell_t_ *after = curr->next;

        /* Split only when the leftover can stand on its own as a block. */
        if (block_total >= total + DS_MIN_BLOCK) {
          _ds_block_header_t_ *rem = (_ds_block_header_t_ *)(void *)((char *)ch + total);
          _ds_free_cell_t_ *rem_cell;
          ds_block_init(rem, block_total - total, NULL, DS_BLOCK_FREE);
          rem_cell = (_ds_free_cell_t_ *)ds_payload_of(rem);
          rem_cell->next = after;
          if (prev)
            prev->next = rem_cell;
          else
            c->free_list_head = rem_cell;
          a->total_free_bytes_in_list -= total;
        } else {
          total = block_total; /* take the whole block */
          if (prev)
            prev->next = after;
          else
            c->free_list_head = after;
          a->total_free_bytes_in_list -= block_total;
        }

        ds_block_init(ch, total, desc, state);
        memset(ds_payload_of(ch), 0, total - sizeof(_ds_block_header_t_));
        a->allocs_from_free_list++;
        if (state == DS_BLOCK_MANAGED) ds_account_live(a, total);
        return ds_payload_of(ch);
      }
      prev = curr;
      curr = curr->next;
    }
    prev_chunk = c;
    c = c->next_free_chunk;
  }

  /* ---- 2. Bump-pointer path ---- */
  chunk = a->head;
  if (!chunk || total > chunk->chunk_size - chunk->chunk_size_used) chunk = ds_arena_grow(a, total);

  h = (_ds_block_header_t_ *)(void *)(chunk->payload + chunk->chunk_size_used);
  chunk->chunk_size_used += total;
  ds_block_init(h, total, desc, state);
  memset(ds_payload_of(h), 0, total - sizeof(_ds_block_header_t_));
  a->allocs_from_bump++;
  if (state == DS_BLOCK_MANAGED) ds_account_live(a, total);
  return ds_payload_of(h);
}

void *ds_arena_alloc_internal(_ds_arena_t_ *a, size_t size) {
  return ds_arena_alloc_block(a, size, NULL, DS_BLOCK_RAW);
}

void *ds_arena_alloc_raw(_ds_arena_t_ *a, size_t size) { return ds_arena_alloc_block(a, size, NULL, DS_BLOCK_RAW); }

void *ds_arena_alloc(_ds_arena_t_ *a, size_t size, const ds_type_descriptor_t *desc) {
  return ds_arena_alloc_block(a, size, desc, DS_BLOCK_MANAGED);
}

/* ------------------------------------------------------------------ */
/* Recycling                                                           */
/* ------------------------------------------------------------------ */

/*
 * The `size` argument is kept for source compatibility but is no longer
 * consulted -- the block's own header is authoritative. Requiring every
 * caller to remember the exact size it had allocated was a standing bug
 * source: ds_graph_remove_vertex used to pass an element count where a byte
 * count was needed, handing the allocator a length that was simply wrong.
 */
void ds_arena_recycle_raw(_ds_arena_t_ *a, void *dead_ptr, size_t size) {
  _ds_block_header_t_ *h;
  _ds_arena_chunk_t_ *c;
  _ds_free_cell_t_ *cell;

  (void)size;
  if (!a || !dead_ptr) return;

  h = ds_block_of(dead_ptr);
  if (h->magic != DS_BLOCK_MAGIC) return; /* not an arena block */
  if (h->state == DS_BLOCK_FREE) return;  /* already recycled */

  c = ds_chunk_owning(a, h);
  if (!c) return;

  if (h->state == DS_BLOCK_MANAGED) {
    a->gc_live_allocations--;
    a->gc_live_bytes -= h->total_size;
  }

  h->state = DS_BLOCK_FREE;
  h->descriptor = NULL;
  h->marked = 0;

  cell = (_ds_free_cell_t_ *)ds_payload_of(h);
  cell->next = c->free_list_head;
  c->free_list_head = cell;
  ds_chunk_mark_free(a, c);
  a->total_free_bytes_in_list += h->total_size;
}

void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr, size_t size) { ds_arena_recycle_raw(a, dead_ptr, size); }

/* ------------------------------------------------------------------ */
/* Teardown and telemetry                                              */
/* ------------------------------------------------------------------ */

void ds_arena_destroy(_ds_arena_t_ *a) {
  _ds_arena_chunk_t_ *c;

  if (!a) return;

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
    const _ds_free_cell_t_ *fc;
    total_arena_bytes += c->chunk_size;
    for (fc = c->free_list_head; fc; fc = fc->next) {
      const _ds_block_header_t_ *fh = ((const _ds_block_header_t_ *)fc) - 1;
      if (fh->total_size > largest_free_block) largest_free_block = fh->total_size;
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
