/*
 * Arena and collector fuzzer.
 *
 * Drives randomised sequences of allocate / recycle / collect and checks
 * the properties the allocator is supposed to guarantee at every step:
 *
 *   - every payload is ARENA_ALIGN aligned and survives pointer tagging;
 *   - no two live blocks overlap;
 *   - freshly handed out memory is zeroed;
 *   - every live block still holds the byte pattern written into it.
 *
 * That last one is the strong one. Each live block is stamped with a
 * pattern derived from its own identity and re-verified after every
 * subsequent operation, so any write that lands outside its own block --
 * a free-list cell scribbled into a neighbour, a bad split, a sweep that
 * frees a reachable block -- is caught on the next check rather than
 * surfacing much later as unrelated nonsense.
 */

#include "ds_arena.h"
#include "ds_fuzz.h"
#include "gc.h"

const char *ds_fuzz_name = "fuzz_arena";

#define MAX_LIVE 256

typedef struct {
  void *ptr;
  size_t size;
  uint8_t stamp;
  int managed;
} block_t;

static block_t live[MAX_LIVE];
static size_t live_count;

static void stamp_block(block_t *b) {
  memset(b->ptr, b->stamp, b->size);
}

static void verify_block(const block_t *b) {
  const unsigned char *p = (const unsigned char *)b->ptr;
  size_t i;

  FZ_CHECK((((uintptr_t)b->ptr) & (ARENA_ALIGN - 1)) == 0, "a live block lost its 16-byte alignment");
  FZ_CHECK(ds_get_ptr(ds_tag_ptr(b->ptr, TYPE_STRING)) == b->ptr,
           "a live block's address does not survive tagging");

  for (i = 0; i < b->size; i++) {
    FZ_CHECK(p[i] == b->stamp, "a live block's contents were modified by something else");
  }
}

static void verify_all(void) {
  size_t i, j;
  for (i = 0; i < live_count; i++) {
    verify_block(&live[i]);
    for (j = i + 1; j < live_count; j++) {
      const char *ai = (const char *)live[i].ptr;
      const char *aj = (const char *)live[j].ptr;
      FZ_CHECK(ai + live[i].size <= aj || aj + live[j].size <= ai, "two live blocks overlap");
    }
  }
}

static void do_alloc(_ds_arena_t_ *a, ds_fuzz_t *f, int managed) {
  size_t size;
  void *p;
  const unsigned char *bytes;
  size_t i;

  if (live_count >= MAX_LIVE) return;

  size = fz_range(f, 1, 600);
  p = managed ? ds_arena_alloc(a, size, NULL) : ds_arena_alloc_raw(a, size);
  FZ_CHECK(p != NULL, "the arena returned NULL");

  bytes = (const unsigned char *)p;
  for (i = 0; i < size; i++) FZ_CHECK(bytes[i] == 0, "freshly allocated memory was not zeroed");

  live[live_count].ptr = p;
  live[live_count].size = size;
  live[live_count].stamp = (uint8_t)(live_count + 1);
  live[live_count].managed = managed;
  stamp_block(&live[live_count]);
  live_count++;
}

static void do_recycle(_ds_arena_t_ *a, ds_fuzz_t *f) {
  size_t idx;

  if (live_count == 0) return;

  idx = fz_range(f, 0, live_count - 1);
  ds_arena_recycle_raw(a, live[idx].ptr, live[idx].size);

  live[idx] = live[live_count - 1];
  live_count--;
}

/* Recycling the same pointer twice must be a no-op, not a corrupted free
 * list. The header records the block's state, which is what makes this
 * detectable at all. */
static void do_double_recycle(_ds_arena_t_ *a, ds_fuzz_t *f) {
  size_t idx;
  void *p;
  size_t sz;

  if (live_count == 0) return;

  idx = fz_range(f, 0, live_count - 1);
  p = live[idx].ptr;
  sz = live[idx].size;

  ds_arena_recycle_raw(a, p, sz);
  ds_arena_recycle_raw(a, p, sz);
  ds_arena_recycle_raw(a, p, 999999); /* and with a wrong size */

  live[idx] = live[live_count - 1];
  live_count--;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  ds_fuzz_t f;
  _ds_arena_t_ *arena;
  size_t chunk_choice;
  int ops;

  if (size < 4) return 0;

  fz_init(&f, data, size);
  live_count = 0;

  /* Vary the chunk size, including sizes far smaller than a single
   * allocation, so the oversized-request and many-chunks paths are hit. */
  chunk_choice = fz_range(&f, 0, 3);
  arena = ds_arena_new(chunk_choice == 0   ? 0
                       : chunk_choice == 1 ? 256
                       : chunk_choice == 2 ? 4096
                                           : 65536);

  for (ops = 0; ops < 400 && !fz_empty(&f); ops++) {
    switch (fz_u8(&f) % 10) {
      case 0:
      case 1:
      case 2:
      case 3: do_alloc(arena, &f, 0); break;
      case 4:
      case 5: do_alloc(arena, &f, 1); break;
      case 6:
      case 7: do_recycle(arena, &f); break;
      case 8: do_double_recycle(arena, &f); break;
      default:
        /* Nothing is registered as a root, so every managed block is
         * unreachable and must be swept -- while every raw block must
         * survive untouched. That distinction is the whole point of the
         * managed/raw split, and it is checked by verify_all() below. */
        {
          size_t i, w = 0;
          ds_arena_run_gc(arena);
          for (i = 0; i < live_count; i++) {
            if (!live[i].managed) live[w++] = live[i];
          }
          live_count = w;
        }
        break;
    }
    verify_all();
  }

  /* Whatever the sequence was, a final collection plus teardown must be
   * clean. */
  ds_arena_run_gc(arena);
  ds_arena_destroy(arena);
  return 0;
}
