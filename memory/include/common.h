#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARENA_ALIGN 16
#define GC_HASH_SIZE 1024

_Static_assert((GC_HASH_SIZE & (GC_HASH_SIZE - 1)) == 0, "GC_HASH_SIZE must be a power of two");

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
} ds_type_t;

#define ARENA_TAG_MASK ((uintptr_t)0xF)
#define ARENA_PTR_MASK (~(uintptr_t)0xF)

typedef uintptr_t ds_node_t;

static inline ds_type_t ds_get_type(ds_node_t n) { return (ds_type_t)(n & ARENA_TAG_MASK); }
static inline void *ds_get_ptr(ds_node_t n) { return (void *)(n & ARENA_PTR_MASK); }
static inline ds_node_t ds_tag_ptr(void *ptr, ds_type_t type) { return (ds_node_t)ptr | type; }
static inline ds_node_t ds_make_int(int val) { return ((ds_node_t)((intptr_t)val << 4)) | TYPE_INT; }
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

// Maille élémentaire de la Free-List locale
typedef struct _ds_free_block_ {
  size_t size;
  struct _ds_free_block_ *next;
} _ds_free_block_t_;

typedef struct _ds_arena_chunk_t_ _ds_arena_chunk_t_;
typedef struct __ds_arena__ _ds_arena_t_;

typedef struct {
  void (*mark)(ds_node_t node, _ds_arena_t_ *a);
  void (*finalize)(void *ptr, _ds_arena_t_ *a);
} ds_type_descriptor_t;

typedef struct _ds_allocation_track_ {
  void *ptr;
  size_t size;
  uint8_t marked;
  const ds_type_descriptor_t *descriptor;
  struct _ds_allocation_track_ *next;
} _ds_allocation_track_t_;

typedef struct _ds_gc_root_ {
  ds_node_t *variable_pointer;
  struct _ds_gc_root_ *next;
} _ds_gc_root_t_;

// --- ARCHITECTURE RECOMMANDÉE : FREE-LIST LOCALISÉE AU CHUNK ---
typedef struct _ds_arena_chunk_t_ {
  struct _ds_arena_chunk_t_ *next_arena_chunk;
  size_t chunk_size;
  size_t chunk_size_used;
  _ds_free_block_t_ *free_list_head;  // Free-List propre et confinée à ce chunk (Aucune fuite inter-chunk)
  size_t reserved;                    // Alignement 16 octets du header (32 octets totaux)
} _ds_arena_chunk_t_;

typedef struct __ds_arena__ {
  _ds_arena_chunk_t_ *head;
  size_t chunk_size;

  _ds_allocation_track_t_ **gc_buckets;
  size_t gc_hash_size;
  _ds_gc_root_t_ *gc_roots;

  ds_node_t *gc_mark_stack;
  size_t gc_mark_stack_top;
  size_t gc_mark_stack_cap;

  size_t allocs_from_bump;
  size_t allocs_from_free_list;
  size_t peak_chunks;
  size_t current_chunks;
  size_t total_free_bytes_in_list;

  size_t gc_live_allocations;
  size_t gc_live_bytes;
  size_t gc_peak_live_bytes;
} _ds_arena_t_;

#endif
