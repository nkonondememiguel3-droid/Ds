/*
 * Hash map: insertion with and without growth, hits, misses, removal.
 *
 * The pre-sized and growing insert cases are deliberately paired -- the
 * difference between them is exactly what the resize chain costs.
 */

#include <stdio.h>

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_hash_map.h"
#include "ds_string.h"

#define N 50000

static ds_string_t **make_keys(_ds_arena_t_ *a, size_t n) {
  ds_string_t **keys = (ds_string_t **)ds_arena_alloc_raw(a, n * sizeof(ds_string_t *));
  char buf[32];
  size_t i;
  for (i = 0; i < n; i++) {
    snprintf(buf, sizeof(buf), "key-id-%zu", i);
    keys[i] = ds_str_new(a, buf);
  }
  return keys;
}

static void bench_put_growing(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t **keys = make_keys(a, N);
    ds_hash_map_t *m = ds_map_new(a, 4); /* forces ~14 resizes */
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_map_put(a, m, keys[i], ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("put, growing from 4 buckets", N);
}

static void bench_put_presized(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t **keys = make_keys(a, N);
    ds_hash_map_t *m = ds_map_new(a, 131072); /* no resize at all */
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_map_put(a, m, keys[i], ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("put, pre-sized (no resize)", N);
}

static void bench_put_overwrite(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t **keys = make_keys(a, N);
    ds_hash_map_t *m = ds_map_new(a, 131072);
    int i;
    for (i = 0; i < N; i++) ds_map_put(a, m, keys[i], ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_map_put(a, m, keys[i], ds_make_int(i * 2));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("put, overwriting existing keys", N);
}

static void bench_get_hit(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t **keys = make_keys(a, N);
    ds_hash_map_t *m = ds_map_new(a, 4);
    int i;
    for (i = 0; i < N; i++) ds_map_put(a, m, keys[i], ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)ds_unpack_int(ds_map_get(m, keys[i])));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("get, every lookup hits", N);
}

static void bench_get_miss(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t **keys = make_keys(a, N);
    ds_hash_map_t *m = ds_map_new(a, 4);
    ds_string_t *absent = ds_str_new(a, "definitely-not-present");
    int i;
    for (i = 0; i < N; i++) ds_map_put(a, m, keys[i], ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)ds_get_type(ds_map_get(m, absent)));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("get, every lookup misses", N);
}

static void bench_remove(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t **keys = make_keys(a, N);
    ds_hash_map_t *m = ds_map_new(a, 131072);
    int i;
    for (i = 0; i < N; i++) ds_map_put(a, m, keys[i], ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep(ds_map_remove(a, m, keys[i]) ? 1u : 0u);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("remove, every key", N);
}

/* A single bucket turns the map into a linked list: the pathological case. */
static void bench_collision_chain(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_string_t **keys = make_keys(a, 2000);
    ds_hash_map_t *m = ds_map_new(a, 1);
    int i;
    for (i = 0; i < 2000; i++) ds_map_put(a, m, keys[i], ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < 2000; i++) ds_bench_keep((size_t)ds_unpack_int(ds_map_get(m, keys[i])));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("get, 2000 keys in one bucket", 2000);
}

int main(void) {
  ds_bench_banner("hash_map");
  bench_put_growing();
  bench_put_presized();
  bench_put_overwrite();
  bench_get_hit();
  bench_get_miss();
  bench_remove();
  bench_collision_chain();
  ds_bench_footer();
  return 0;
}
