#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARENA_ALIGN 16

typedef enum { TYPE_INT = 0, TYPE_FLOAT = 1, TYPE_BOOL = 2, TYPE_NIL = 3, TYPE_STRING = 4, TYPE_NODE = 5 } NodeType;

#define ARENA_TAG_MASK ((uintptr_t)0xF)
#define ARENA_PTR_MASK (~(uintptr_t)0xF)

typedef uintptr_t ds_node_t;

static inline NodeType ds_get_type(ds_node_t n) { return (NodeType)(n & ARENA_TAG_MASK); }
static inline void *ds_get_ptr(ds_node_t n) { return (void *)(n & ARENA_PTR_MASK); }
static inline ds_node_t ds_tag_ptr(void *ptr, NodeType type) { return (ds_node_t)ptr | type; }
static inline ds_node_t ds_make_int(int val) { return ((uintptr_t)val << 4) | TYPE_INT; }

static inline int ds_unpack_int(ds_node_t node) { return (int)((intptr_t)node >> 4); }

static inline ds_node_t ds_make_float(float val) {
  uint32_t bits;
  memcpy(&bits, &val, sizeof(bits));
  return ((uintptr_t)bits << 4) | TYPE_FLOAT;
}

static inline float ds_unpack_float(ds_node_t node) {
  uint32_t bits = (uint32_t)(node >> 4);
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

typedef struct _ds_free_cell_ {
  ds_node_t next_free;
} _ds_free_cell_t_;

typedef void *(*ds_mem_alloc_func)(size_t bytes, void *user_data);
typedef void *(*ds_mem_realloc_func)(void *ptr, size_t new_size, void *user_data);
typedef void (*ds_mem_free_func)(void *ptr, void *user_data);

typedef struct {
  ds_mem_alloc_func alloc;
  ds_mem_realloc_func realloc;
  ds_mem_free_func free;
  void *context;
} _adjust_memory_interface_t_;

typedef struct __ds_arena_chunk__ {
  struct __ds_arena_chunk__ *next_arena_chunk;
  size_t chunk_size;
  size_t chunk_size_used;
} _ds_arena_chunk_t_;

typedef struct _ds_free_block_ {
  void *address;
  struct _ds_free_block_ *next;
} _ds_free_block_t_;

typedef struct {
  _ds_arena_chunk_t_ *head;
  _adjust_memory_interface_t_ imemory;
  size_t chunk_size;

  _ds_free_block_t_ *free_list_head;

  size_t allocs_from_bump;
  size_t allocs_from_free_list;
} _ds_arena_t_;

#endif
