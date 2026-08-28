#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ds_platform.h"

#define ARENA_ALIGN 16
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

typedef struct _ds_arena_chunk_t_ _ds_arena_chunk_t_;
typedef struct __ds_arena__ _ds_arena_t_;

typedef struct {
  void (*mark)(ds_node_t node, _ds_arena_t_ *a);
  void (*finalize)(void *ptr, _ds_arena_t_ *a);
} ds_type_descriptor_t;

/*
 * ------------------------------------------------------------------
 * Block header
 * ------------------------------------------------------------------
 *
 * Every block the arena hands out -- managed or raw -- is preceded by one
 * of these, and every byte of a chunk's used region is covered by exactly
 * one block. That single invariant replaces the side hash table the
 * collector used to keep:
 *
 *   - registering a managed object is three field writes into a cache line
 *     the allocator just touched, instead of malloc() + pointer hash +
 *     bucket insert;
 *   - marking is a pointer subtraction instead of a hash and a chain walk;
 *   - sweeping is a linear walk of each chunk in address order, which also
 *     makes coalescing adjacent dead blocks fall out for free.
 *
 * The header is exactly ARENA_ALIGN bytes, so a 16-byte-aligned block start
 * still yields a 16-byte-aligned payload and the four low tag bits stay
 * free.
 */

#define DS_BLOCK_MAGIC ((uint16_t)0xD5A1)

enum {
  DS_BLOCK_FREE = 0,   /* on a chunk free list */
  DS_BLOCK_RAW = 1,    /* live, untracked by the collector */
  DS_BLOCK_MANAGED = 2 /* live, traced and swept */
};

typedef struct _ds_block_header_ {
  const ds_type_descriptor_t *descriptor; /* NULL for raw, or a leaf managed object */
  uint32_t total_size;                    /* header + payload, always ARENA_ALIGN-aligned */
  uint16_t magic;
  uint8_t state;
  uint8_t marked;
#if UINTPTR_MAX <= 0xFFFFFFFFu
  uint32_t reserved_; /* pad the ILP32 layout back up to ARENA_ALIGN */
#endif
} _ds_block_header_t_;

DS_STATIC_ASSERT(sizeof(_ds_block_header_t_) == ARENA_ALIGN,
                 "the block header must be exactly ARENA_ALIGN so payloads stay taggable");

/* Free-list link, written into the payload area of a dead block. The block's
 * size lives in its header, so the cell itself is a single pointer. */
typedef struct _ds_free_cell_ {
  struct _ds_free_cell_ *next;
} _ds_free_cell_t_;

DS_STATIC_ASSERT(sizeof(_ds_free_cell_t_) <= ARENA_ALIGN, "a recycled block must be able to hold a free-list cell");

/* Smallest block the allocator will ever produce: header plus one aligned
 * payload granule, which is also the smallest block that can hold a cell. */
#define DS_MIN_BLOCK (sizeof(_ds_block_header_t_) + ARENA_ALIGN)

static DS_INLINE _ds_block_header_t_ *ds_block_of(void *payload) { return ((_ds_block_header_t_ *)payload) - 1; }

static DS_INLINE void *ds_payload_of(_ds_block_header_t_ *h) { return (void *)(h + 1); }

typedef struct _ds_gc_root_ {
  ds_node_t *variable_pointer;
  struct _ds_gc_root_ *next;
} _ds_gc_root_t_;

/* --- Chunk with a chunk-local free list (no cross-chunk leakage) --- */
struct _ds_arena_chunk_t_ {
  struct _ds_arena_chunk_t_ *next_arena_chunk;
  /* Chunks holding at least one free block are threaded onto a second list
   * so the allocator never walks chunks that have nothing to offer. */
  struct _ds_arena_chunk_t_ *next_free_chunk;
  uint8_t on_free_ring;
  size_t chunk_size;
  size_t chunk_size_used;
  _ds_free_cell_t_ *free_list_head;
  /* Payload base, aligned up to ARENA_ALIGN. Storing it explicitly is what
   * guarantees the 16-byte invariant the whole tagging scheme depends on;
   * the old code used (chunk + 1), which lands on a 40-byte offset on LP64
   * and therefore handed out 8-byte-aligned memory. */
  char *payload;
};

struct __ds_arena__ {
  _ds_arena_chunk_t_ *head;
  _ds_arena_chunk_t_ *free_chunks; /* chunks with a non-empty free list */
  size_t chunk_size;

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
