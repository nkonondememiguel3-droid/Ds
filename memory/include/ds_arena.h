#ifndef ds_arena_h
#define ds_arena_h

#include <stdio.h>

// Memory alignment.
#define ARENA_ALIGN 16

#define ARENA_DEFAULT_CHUNK_SIZE (1024u * 1024u)

typedef void *(*ds_mem_alloc_func)(size_t bytes, void *user_data);
typedef void *(*ds_mem_realloc_func)(void *ptr, size_t new_size, void *user_data);
typedef void (*ds_mem_free_func)(void *ptr, void *user_data);

typedef struct {
  ds_mem_alloc_func alloc;
  ds_mem_realloc_func realloc;
  ds_mem_free_func free;
  void *context;
} _adjust_memory_interface_t_;

// A chunk of memory allocated in an arena.
typedef struct __ds_arena_chunk__ {
  struct __ds_arena_chunk__ *next_arena_chunk;

  size_t chunk_size;
  size_t chunk_size_used;

  // Payload follows immediately after the header in memory.
} _ds_arena_chunk_t_;

typedef struct {
  _ds_arena_chunk_t_ *head;
  _adjust_memory_interface_t_ imemory;

  size_t chunk_size;  // The size of each chunk of memory in the arena.
} _ds_arena_t_;

extern _ds_arena_t_ ds_arena_new(size_t chunk_size);
extern size_t ds_arena_align_up(size_t n);
extern void *ds_arena_alloc(_ds_arena_t_ *a, size_t size);
extern void ds_arena_destroy(_ds_arena_t_ *a);

// Allocate and zero one T from the arena.
#define ARENA_NEW(a, T) ((T *)ds_arena_alloc((a), sizeof(T)))

// Allocate and zero n elements of type T.
#define ARENA_ARRAY(a, T, n) ((T *)ds_arena_alloc((a), sizeof(T) * (size_t)(n)))

// Checkpoint / restore.
typedef struct {
  _ds_arena_chunk_t_ *checkpoint_head;

  size_t checkpoint_size_used;
} _ds_arena_checkpoint_t_;

extern _ds_arena_checkpoint_t_ ds_arena_checkpoint(_ds_arena_t_ *a);
extern void ds_arena_reset_to(_ds_arena_t_ *a, _ds_arena_checkpoint_t_ cp);

// Custom memory
_ds_arena_t_ ds_arena_new_with_allocator(size_t chunk_size, ds_mem_alloc_func m_alloc, ds_mem_realloc_func m_realloc,
                                         ds_mem_free_func m_free, void *context);

#endif  // ds_arena_h
