#ifndef ds_arena_h
#define ds_arena_h

/*
 * The arena: a chunked bump allocator with per-chunk free lists.
 *
 * This layer is self-contained. It hands out aligned blocks of bytes, takes
 * them back, and tracks where they went; it does not know what a value is,
 * what a root is, or how anything gets collected. Link libds_arena.a on its
 * own and none of the collector is pulled in.
 *
 * What the arena does provide is the substrate a collector needs, so that a
 * collector can be a separate layer rather than a rewrite:
 *
 *   - every block carries a header with a state, a mark bit and one opaque
 *     metadata pointer, all in the ARENA_ALIGN bytes before the payload;
 *   - blocks can be moved between the traced and untraced states;
 *   - the free lists can be rebuilt from block state alone.
 *
 * The arena never dereferences the metadata pointer and never clears the
 * mark bit of a live block. Those are the collector's business -- see gc.h,
 * which is one possible policy on top of this mechanism, not a requirement
 * of it.
 */

#include <stddef.h>
#include <stdint.h>

#include "ds_platform.h"

#define ARENA_ALIGN 16
#define ARENA_DEFAULT_CHUNK_SIZE (1024u * 1024u)

/* How many free-list cells a single allocation is willing to walk before it
 * gives up and takes the bump-pointer path. Without a bound, allocation
 * degrades to O(number of free blocks) and the whole arena goes quadratic
 * once a workload frees a lot. */
#define DS_FREELIST_SCAN_LIMIT 32

DS_STATIC_ASSERT((ARENA_ALIGN & (ARENA_ALIGN - 1)) == 0, "ARENA_ALIGN must be a power of two");
DS_STATIC_ASSERT(ARENA_ALIGN >= 16, "ARENA_ALIGN must be large enough to hold a block header");

/* Kept for source compatibility with the original header. */
#define ALIGN16 DS_ALIGNAS(ARENA_ALIGN)
#define ALIGN16_POST

typedef struct _ds_arena_chunk_t_ _ds_arena_chunk_t_;
typedef struct __ds_arena__ _ds_arena_t_;

/*
 * Opaque to this layer. A collector completes the type (gc.h does) and the
 * arena only ever stores and returns the pointer.
 */
typedef struct ds_type_descriptor ds_type_descriptor_t;

/*
 * ------------------------------------------------------------------
 * Block header
 * ------------------------------------------------------------------
 *
 * Every block the arena hands out -- managed or raw -- is preceded by one
 * of these, and every byte of a chunk's used region is covered by exactly
 * one block. That single invariant is what lets a collector live outside
 * the arena without a side table of its own:
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
  DS_BLOCK_RAW = 1,    /* live, untracked by any collector */
  DS_BLOCK_MANAGED = 2 /* live, traced and swept by whatever collector is attached */
};

typedef struct _ds_block_header_ {
  const ds_type_descriptor_t *descriptor; /* collector metadata; NULL for raw, opaque here */
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

  size_t allocs_from_bump;
  size_t allocs_from_free_list;
  size_t peak_chunks;
  size_t current_chunks;
  size_t total_free_bytes_in_list;

  /* Blocks in the DS_BLOCK_MANAGED state. The arena maintains the count
   * because it is the only thing that sees every state transition; it draws
   * no conclusions from it. */
  size_t live_managed_allocations;
  size_t live_managed_bytes;
  size_t peak_live_managed_bytes;

  /*
   * A collector attaches itself here. The arena treats the state as opaque
   * and only calls the teardown hook from ds_arena_destroy, so an arena with
   * no collector linked in leaves both NULL and costs one branch at destroy.
   */
  void *collector;
  void (*collector_destroy)(_ds_arena_t_ *a);
};

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

_ds_arena_t_ *ds_arena_new(size_t chunk_size);
void ds_arena_destroy(_ds_arena_t_ *a);
void ds_arena_print_stats(const _ds_arena_t_ *a);

size_t ds_arena_align_up(size_t n);

/* ------------------------------------------------------------------ */
/* Allocation                                                          */
/* ------------------------------------------------------------------ */

/* Unmanaged: never traced, never swept, released only by an explicit
 * ds_arena_recycle_raw or by destroying the arena. This is the whole of the
 * allocation API for a program that uses the arena on its own. */
void *ds_arena_alloc_raw(_ds_arena_t_ *a, size_t size);
void *ds_arena_alloc_internal(_ds_arena_t_ *a, size_t size);

/* Managed: the block is stamped DS_BLOCK_MANAGED and `desc` is recorded in
 * its header. The arena still only allocates -- with no collector attached
 * such a block simply behaves like a raw one that nobody frees. */
void *ds_arena_alloc(_ds_arena_t_ *a, size_t size, const ds_type_descriptor_t *desc);

void ds_arena_recycle_raw(_ds_arena_t_ *a, void *dead_ptr, size_t size);
void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr, size_t size);

#define ARENA_NEW(a, T, desc) ((T *)ds_arena_alloc((a), sizeof(T), (desc)))
#define ARENA_ARRAY(a, T, n) ((T *)ds_arena_alloc_raw((a), sizeof(T) * (size_t)(n)))

/* ------------------------------------------------------------------ */
/* Hooks for a collector                                               */
/* ------------------------------------------------------------------ */
/*
 * Nothing below is needed to use the arena by itself. It exists so that a
 * collector can be built on top without reaching into the accounting: the
 * collector decides which blocks are dead, and these keep the arena's own
 * bookkeeping correct as it does.
 */

/* Move a live block into / out of the traced state. Returns 0 if `ptr` is
 * not an arena block or is already in the requested state. */
int ds_arena_block_promote(_ds_arena_t_ *a, void *ptr, const ds_type_descriptor_t *desc);
int ds_arena_block_demote(_ds_arena_t_ *a, void *ptr);

/* Unlink every free list. Allocation falls through to the bump pointer until
 * the lists are rebuilt, which is always safe. */
void ds_arena_reset_free_lists(_ds_arena_t_ *a);

/* Rebuild every free list from block state alone, coalescing runs of
 * adjacent free blocks as it goes. Idempotent, and independent of how the
 * blocks came to be free. */
void ds_arena_rebuild_free_lists(_ds_arena_t_ *a);

#ifdef __cplusplus
}
#endif

/*
 * Retire a managed block: drop it from the live accounting and mark it
 * DS_BLOCK_FREE *without* linking it into any free list, and hand back the
 * descriptor it carried so the caller can run a finalizer. Leaving the
 * linking out is what lets a sweep finalize and rebuild in separate passes.
 * Use ds_arena_rebuild_free_lists afterwards to make the space available.
 *
 * Inline because a sweep calls it once per dead block: as an out-of-line
 * call in another translation unit it cost the collector's sweep about 18%.
 * The body is six field writes on structs this header already declares, so
 * inlining it gives up nothing -- the descriptor stays opaque either way.
 */
static DS_INLINE const ds_type_descriptor_t *ds_arena_block_retire(_ds_arena_t_ *a, _ds_block_header_t_ *h) {
  const ds_type_descriptor_t *desc;

  if (!a || !h || h->state != DS_BLOCK_MANAGED) return NULL;

  desc = h->descriptor;
  a->live_managed_allocations--;
  a->live_managed_bytes -= h->total_size;
  h->state = DS_BLOCK_FREE;
  h->descriptor = NULL;
  h->marked = 0;
  return desc;
}

#endif
