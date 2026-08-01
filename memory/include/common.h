#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARENA_ALIGN 16

#if defined(_MSC_VER)
#define ALIGN16 __declspec(align(16))
#define ALIGN16_POST
#else
#define ALIGN16
#define ALIGN16_POST __attribute__((aligned(ARENA_ALIGN)))
#endif

typedef enum {
  TYPE_INT = 0,
  TYPE_FLOAT = 1,
  TYPE_BOOL = 2,
  TYPE_NIL = 3,
  TYPE_STRING = 4,
  TYPE_NODE = 5,
} NodeType;

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

// Free-List intrusive (Overlay)
typedef struct _ds_free_block_ {
  size_t size;
  struct _ds_free_block_ *next;
} _ds_free_block_t_;

typedef struct _ds_allocation_track_ {
  void *ptr;
  size_t size;
  uint8_t marked;
  struct _ds_allocation_track_ *next;
} _ds_allocation_track_t_;

typedef struct _ds_gc_root_ {
  ds_node_t *variable_pointer;
  struct _ds_gc_root_ *next;
} _ds_gc_root_t_;

typedef void (*ds_gc_mark_extension_func)(ds_node_t node);

// --- EXCELLENCE 3 : TAILLE DE HEADER MULTIPLE DE ARENA_ALIGN ---
// 8 + 8 + 8 + 8 = 32 octets. Pas besoin de directives d'alignement complexes.
typedef struct __ds_arena_chunk__ {
  struct __ds_arena_chunk__ *next_arena_chunk;
  size_t chunk_size;
  size_t chunk_size_used;
  size_t reserved;  // Rembourrage structurel strict
} _ds_arena_chunk_t_;

typedef struct {
  _ds_arena_chunk_t_ *head;
  size_t chunk_size;

  _ds_free_block_t_ *free_list_head;

  _ds_gc_root_t_ *gc_roots;
  _ds_allocation_track_t_ *gc_allocs;
  ds_gc_mark_extension_func gc_custom_mark_callback;

  size_t allocs_from_bump;
  size_t allocs_from_free_list;
  size_t peak_chunks;
  size_t current_chunks;
  size_t total_free_bytes_in_list;
} _ds_arena_t_;

#endif
