/*
 * The arena allocator: block headers, alignment, reuse, coalescing.
 *
 * This suite links libds_arena.a and nothing else -- no collector, no data
 * structures. If the arena ever grows a dependency on gc.h it stops linking,
 * which is the cheapest possible guard on the split between the two.
 */

#include <string.h>

#include "ds_arena.h"
#include "ds_test.h"
#include "ds_value.h"

static size_t largest_free(const _ds_arena_t_ *a) {
  const _ds_arena_chunk_t_ *c;
  size_t largest = 0;
  for (c = a->head; c; c = c->next_arena_chunk) {
    const _ds_free_cell_t_ *fc;
    for (fc = c->free_list_head; fc; fc = fc->next) {
      size_t sz = (((const _ds_block_header_t_ *)fc) - 1)->total_size;
      if (sz > largest) largest = sz;
    }
  }
  return largest;
}

static void test_alignment(void) {
  _ds_arena_t_ *a = ds_arena_new(4096);
  int i;
  int ok = 1;

  SECTION("16-byte alignment");

  /* This is the invariant the whole tagging scheme rests on. The chunk
   * header used to be 40 bytes on LP64 and the payload started right after
   * it, so every allocation came back 8-byte aligned and TYPE_STRING /
   * TYPE_NODE tags corrupted real address bits. */
  for (i = 0; i < 500; i++) {
    void *p = ds_arena_alloc_raw(a, (size_t)(i % 97) + 1);
    if (((uintptr_t)p & (ARENA_ALIGN - 1)) != 0) ok = 0;
    if (ds_get_ptr(ds_tag_ptr(p, TYPE_STRING)) != p) ok = 0;
  }
  CHECK(ok, "regression: every allocation is 16-byte aligned and survives tagging");

  ds_arena_destroy(a);
}

static void test_block_headers(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  void *raw, *managed;
  size_t free_before;

  SECTION("block headers");

  CHECK(sizeof(_ds_block_header_t_) == ARENA_ALIGN,
        "the header is exactly one alignment granule, so payloads stay taggable");

  raw = ds_arena_alloc_raw(a, 64);
  managed = ds_arena_alloc(a, 64, NULL);
  CHECK(ds_block_of(raw)->state == DS_BLOCK_RAW, "a raw allocation is marked raw");
  CHECK(ds_block_of(managed)->state == DS_BLOCK_MANAGED, "a managed allocation is marked managed");
  CHECK(ds_block_of(raw)->magic == DS_BLOCK_MAGIC, "the header carries its magic");
  CHECK(ds_block_of(raw)->total_size == 64 + ARENA_ALIGN, "total_size covers header plus payload");
  CHECK(a->live_managed_allocations == 1, "only the managed block counts as live");

  /* The size argument to recycle is advisory now: the header is
   * authoritative. Requiring callers to remember their own allocation size
   * was the mechanism behind the ds_graph_remove_vertex bug. */
  ds_arena_recycle_raw(a, raw, 999999);
  CHECK(a->total_free_bytes_in_list == 64 + ARENA_ALIGN,
        "regression: a wrong size passed to recycle is ignored in favour of the header");

  free_before = a->total_free_bytes_in_list;
  ds_arena_recycle_raw(a, raw, 64);
  CHECK(a->total_free_bytes_in_list == free_before, "recycling the same block twice is a no-op");

  ds_arena_destroy(a);
}

static void test_free_list_reuse(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  void *first, *reused;

  SECTION("free-list reuse");

  first = ds_arena_alloc_raw(a, 64);
  memset(first, 0xAB, 64);
  ds_arena_recycle_raw(a, first, 64);
  CHECK(a->total_free_bytes_in_list >= 64, "recycled bytes land in the free list");

  reused = ds_arena_alloc_raw(a, 64);
  CHECK(reused == first, "the free list is consulted before the bump pointer");
  CHECK(((const unsigned char *)reused)[0] == 0, "reused memory is zeroed");
  CHECK(a->allocs_from_free_list == 1, "the reuse is counted");

  /* A block smaller than a free-list cell used to be written straight into
   * the neighbouring allocation's storage. */
  {
    void *tiny = ds_arena_alloc_raw(a, 1);
    void *guard = ds_arena_alloc_raw(a, 16);
    memset(guard, 0x5A, 16);
    ds_arena_recycle_raw(a, tiny, 1);
    CHECK(((unsigned char *)guard)[0] == 0x5A,
          "regression: recycling a sub-16-byte block does not scribble on its neighbour");
  }

  ds_arena_destroy(a);
}

static void test_chunk_growth(void) {
  _ds_arena_t_ *a = ds_arena_new(1024);
  int i;
  int ok = 1;

  SECTION("chunk growth");

  /* Requests larger than the configured chunk size must still work. */
  for (i = 0; i < 20; i++) {
    void *p = ds_arena_alloc_raw(a, 8192);
    if (!p || ((uintptr_t)p & (ARENA_ALIGN - 1)) != 0) ok = 0;
    memset(p, 1, 8192);
  }
  CHECK(ok, "allocations larger than the chunk size get their own chunk");
  CHECK(a->current_chunks >= 20, "oversized allocations grow the chunk list");
  CHECK(a->peak_chunks >= a->current_chunks, "peak tracks current");

  ds_arena_destroy(a);
}

static void test_coalescing(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  void *blocks[200];
  int i;
  size_t expected;

  SECTION("coalescing");

  for (i = 0; i < 200; i++) blocks[i] = ds_arena_alloc_raw(a, 48);
  expected = 200 * (48 + ARENA_ALIGN);

  /* Recycling links a block onto its chunk's list; it does not merge.
   * Straight after this the free space is 200 unusable crumbs. */
  for (i = 0; i < 200; i++) ds_arena_recycle_raw(a, blocks[i], 48);
  CHECK(a->total_free_bytes_in_list == expected, "every recycled byte is accounted for");
  CHECK(largest_free(a) == 48 + ARENA_ALIGN, "recycling on its own leaves the blocks unmerged");

  /* The rebuild walks each chunk in address order, so neighbours are
   * consecutive iterations and merge. A collector's sweep finishes with this
   * pass, but it is arena machinery and works with no collector in sight. */
  ds_arena_rebuild_free_lists(a);
  CHECK(largest_free(a) >= expected * 9 / 10, "regression: adjacent free blocks coalesce into one large block");
  CHECK(a->total_free_bytes_in_list == expected, "the rebuilt list still accounts for every free byte");

  {
    void *big = ds_arena_alloc_raw(a, expected / 2);
    CHECK(big != NULL && a->allocs_from_free_list > 0, "the coalesced region is reusable");
  }

  ds_arena_destroy(a);
}

static void test_no_collector(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  void *managed;

  SECTION("no collector attached");

  /* ds_arena_alloc records a descriptor and a state; interpreting them is
   * somebody else's job. Nothing here attaches a collector, and destroying
   * the arena must not go looking for one. */
  managed = ds_arena_alloc(a, 64, NULL);
  CHECK(managed != NULL, "a managed allocation works without a collector");
  CHECK(a->collector == NULL && a->collector_destroy == NULL, "the arena never attaches a collector by itself");
  CHECK(a->live_managed_allocations == 1, "the block is counted as managed all the same");

  /* Promote/demote is the hook a collector uses; it is pure arena
   * bookkeeping and needs none of one. */
  {
    void *raw = ds_arena_alloc_raw(a, 32);
    CHECK(ds_arena_block_promote(a, raw, NULL) == 1, "a raw block can be promoted");
    CHECK(a->live_managed_allocations == 2, "promotion updates the live count");
    CHECK(ds_arena_block_promote(a, raw, NULL) == 0, "promoting twice is a no-op");
    CHECK(ds_arena_block_demote(a, raw) == 1, "a managed block can be demoted");
    CHECK(a->live_managed_allocations == 1, "demotion updates the live count");
    CHECK(ds_arena_block_demote(a, raw) == 0, "demoting twice is a no-op");
  }

  ds_arena_destroy(a);
}

static void test_null_safety(void) {
  SECTION("null safety");
  ds_arena_destroy(NULL);
  ds_arena_print_stats(NULL);
  ds_arena_recycle_raw(NULL, NULL, 0);
  CHECK(ds_arena_align_up(1) == ARENA_ALIGN, "align_up rounds up to the granule");
  CHECK(ds_arena_align_up(0) == 0, "align_up leaves zero alone");
  CHECK(ds_arena_align_up(ARENA_ALIGN) == ARENA_ALIGN, "align_up is idempotent on a boundary");
  CHECK(1, "the NULL-arena entry points return instead of dereferencing");
}

int main(void) {
  ds_test_begin("arena");
  test_alignment();
  test_block_headers();
  test_free_list_reuse();
  test_chunk_growth();
  test_coalescing();
  test_no_collector();
  test_null_safety();
  return ds_test_end();
}
