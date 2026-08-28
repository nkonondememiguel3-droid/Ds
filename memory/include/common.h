#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ds_platform.h"

#define ARENA_ALIGN 16
#define GC_HASH_SIZE 1024

/* How many free-list cells a single allocation is willing to walk before it
 * gives up and takes the bump-pointer path. Without a bound, allocation
 * degrades to O(number of free blocks) and the whole arena goes quadratic
 * once a workload frees a lot. */
#define DS_FREELIST_SCAN_LIMIT 32

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define DS_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define DS_STATIC_ASSERT(cond, msg) typedef char ds_static_assert_##__LINE__[(cond) ? 1 : -1]
#endif

DS_STATIC_ASSERT((GC_HASH_SIZE & (GC_HASH_SIZE - 1)) == 0, "GC_HASH_SIZE must be a power of two");
DS_STATIC_ASSERT((ARENA_ALIGN & (ARENA_ALIGN - 1)) == 0, "ARENA_ALIGN must be a power of two");
DS_STATIC_ASSERT(ARENA_ALIGN >= 16, "ARENA_ALIGN must leave 4 low bits free for pointer tags");

/* Kept for source compatibility with the original header. */
#define ALIGN16 DS_ALIGNAS(ARENA_ALIGN)
#define ALIGN16_POST

typedef enum {
  TYPE_INT = 0,
  TYPE_FLOAT = 1,
  TYPE_BOOL = 2,
  TYPE_NIL = 3,
  TYPE_STRING = 4,
  TYPE_NODE = 5,
} ds_type_t;

#define ARENA_TAG_MASK ((uint64_t)0xF)
#define ARENA_PTR_MASK (~(uint64_t)0xF)

/*
 * ds_node_t is a 64-bit tagged word, NOT uintptr_t.
 *
 * The original typedef was uintptr_t, which works on LP64 but silently
 * corrupts data on any 32-bit target: ds_make_float shifts a 32-bit IEEE-754
 * pattern left by 4, so the top 4 bits of every float fell off the end of a
 * 32-bit word, and ds_make_int had the same problem for large integers.
 * A fixed 64-bit word holds a pointer on every supported target and leaves
 * room for the tag on all of them.
 */
typedef uint64_t ds_node_t;

static DS_INLINE ds_type_t ds_get_type(ds_node_t n) { return (ds_type_t)(n & ARENA_TAG_MASK); }

static DS_INLINE void *ds_get_ptr(ds_node_t n) { return (void *)(uintptr_t)(n & ARENA_PTR_MASK); }

static DS_INLINE ds_node_t ds_tag_ptr(void *ptr, ds_type_t type) {
  return ((ds_node_t)(uintptr_t)ptr) | (ds_node_t)type;
}

/* Left-shifting a negative signed value is undefined behaviour, so the shift
 * is done on the unsigned word and only the unpack reinterprets the sign. */
static DS_INLINE ds_node_t ds_make_int(int val) { return (((ds_node_t)(uint32_t)val) << 4) | (ds_node_t)TYPE_INT; }

static DS_INLINE int ds_unpack_int(ds_node_t node) { return (int)(uint32_t)(node >> 4); }

static DS_INLINE ds_node_t ds_make_float(float val) {
  uint32_t bits;
  memcpy(&bits, &val, sizeof(bits));
  return (((ds_node_t)bits) << 4) | (ds_node_t)TYPE_FLOAT;
}

static DS_INLINE float ds_unpack_float(ds_node_t node) {
  uint32_t bits = (uint32_t)(node >> 4);
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

static DS_INLINE ds_node_t ds_make_bool(bool val) { return (((ds_node_t)(val ? 1u : 0u)) << 4) | (ds_node_t)TYPE_BOOL; }

static DS_INLINE bool ds_unpack_bool(ds_node_t node) { return ((node >> 4) & 1u) != 0u; }

static DS_INLINE ds_node_t ds_make_nil(void) { return (ds_node_t)TYPE_NIL; }

/* Free-list cell. Overlaid on the dead block itself, so every recycled block
 * must be at least this large -- see the static assert below. */
typedef struct _ds_free_block_ {
  size_t size;
  struct _ds_free_block_ *next;
} _ds_free_block_t_;

DS_STATIC_ASSERT(sizeof(_ds_free_block_t_) <= ARENA_ALIGN, "a recycled block must be able to hold a free-list cell");

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

/* --- Chunk with a chunk-local free list (no cross-chunk leakage) --- */
struct _ds_arena_chunk_t_ {
  struct _ds_arena_chunk_t_ *next_arena_chunk;
  size_t chunk_size;
  size_t chunk_size_used;
  _ds_free_block_t_ *free_list_head;
  /* Payload base, aligned up to ARENA_ALIGN. Storing it explicitly is what
   * guarantees the 16-byte invariant the whole tagging scheme depends on;
   * the old code used (chunk + 1), which lands on a 40-byte offset on LP64
   * and therefore handed out 8-byte-aligned memory. */
  char *payload;
};

struct __ds_arena__ {
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
};

#endif
