#include "ds_arena.h"

#include <mkl.h>
#include <mkl_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  _ds_arena_t_ arena = (_ds_arena_t_){
      .head = NULL,
      .chunk_size = chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE,
  };

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

  // Insert into the linked list of the memory arena. (at the head).
  c->next_arena_chunk = a->head;
  a->head = c;

  return c;
}

inline void *ds_arena_alloc(_ds_arena_t_ *a, size_t size) {
  size = ds_arena_align_up(size);

  _ds_arena_chunk_t_ *c = a->head;
  // if it's the first allocation or the memory used in this chunk plus the
  // size we want is greater than the size of the chunk, we allocate a new
  // bigger bloc of memory.
  if (!c || c->chunk_size_used + size > c->chunk_size) c = ds_arena_grow(a, size);

  void *ptr = (char *)(c + 1) + c->chunk_size_used;
  c->chunk_size_used += size;

  memset(ptr, 0, size);
  return ptr;
}

inline void ds_arena_destroy(_ds_arena_t_ *a) {
  _ds_arena_chunk_t_ *c = a->head;

  while (c) {
    _ds_arena_chunk_t_ *next = c->next_arena_chunk;
    a->imemory.free(c, NULL);
    c = next;
  }
  a->head = NULL;
}

// Checkpoint / restore.
inline _ds_arena_checkpoint_t_ ds_arena_checkpoint(_ds_arena_t_ *a) {
  return (_ds_arena_checkpoint_t_){
      .checkpoint_head = a->head,
      .checkpoint_size_used = a->head ? a->head->chunk_size_used : 0,
  };
}

inline void ds_arena_reset_to(_ds_arena_t_ *a, _ds_arena_checkpoint_t_ cp) {
  while (a->head && a->head != cp.checkpoint_head) {
    _ds_arena_chunk_t_ *dead = a->head;
    a->head = dead->next_arena_chunk;
    a->imemory.free(dead, NULL);
  }

  if (a->head) a->head->chunk_size_used = cp.checkpoint_size_used;
}

_ds_arena_t_ ds_arena_new_with_allocator(size_t chunk_size, ds_mem_alloc_func m_alloc,
                                        ds_mem_realloc_func m_realloc, ds_mem_free_func m_free,
                                        void *context) {
  _ds_arena_t_ arena = (_ds_arena_t_){
      .head = NULL,
      .chunk_size = chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE,
  };

  arena.imemory.alloc = m_alloc;
  arena.imemory.realloc = m_realloc;
  arena.imemory.free = m_free;

  return arena;
}
