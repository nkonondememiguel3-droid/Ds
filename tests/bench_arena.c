/* Allocator throughput: bump path, free-list reuse, chunk growth. */

#include <string.h>

#include "ds_arena.h"
#include "ds_bench.h"

#define N 200000

static void bench_bump(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)(uintptr_t)ds_arena_alloc_raw(a, 32));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("raw alloc, pure bump pointer", N);
}

static void bench_freelist(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    int i;
    for (i = 0; i < 10000; i++) ds_arena_recycle_raw(a, ds_arena_alloc_raw(a, 32), 32);
    ds_bench_start();
    for (i = 0; i < N; i++) {
      void *p = ds_arena_alloc_raw(a, 32);
      ds_arena_recycle_raw(a, p, 32);
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("raw alloc + recycle, free-list reuse", N);
}

static void bench_managed(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)(uintptr_t)ds_arena_alloc(a, 32, NULL));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("managed alloc (traced by the collector)", N);
}

static void bench_mixed_sizes(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)(uintptr_t)ds_arena_alloc_raw(a, (size_t)(i % 512) + 1));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("raw alloc, sizes 1-512 round robin", N);
}

static void bench_chunk_growth(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(4096); /* tiny chunks force constant growth */
    int i;
    ds_bench_start();
    for (i = 0; i < 20000; i++) ds_bench_keep((size_t)(uintptr_t)ds_arena_alloc_raw(a, 2048));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("alloc across many small chunks", 20000);
}

static void bench_memset_cost(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    int i;
    ds_bench_start();
    for (i = 0; i < 20000; i++) ds_bench_keep((size_t)(uintptr_t)ds_arena_alloc_raw(a, 2048));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("alloc of 2 KiB blocks (zeroing dominates)", 20000);
}

int main(void) {
  ds_bench_banner("arena");
  bench_bump();
  bench_freelist();
  bench_managed();
  bench_mixed_sizes();
  bench_chunk_growth();
  bench_memset_cost();
  ds_bench_footer();
  return 0;
}
