/*
 * String operations, with substring search broken out by haystack size.
 *
 * Run twice to compare the two search paths:
 *     bench_string
 *     DS_NO_AVX2=1 bench_string
 */

#include <string.h>

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_dyn_array.h"
#include "ds_string.h"

#define ITERS 200000

static ds_string_t *make_haystack(_ds_arena_t_ *a, size_t len, const char *needle) {
  char *buf = (char *)ds_arena_alloc_raw(a, len + 1);
  size_t nlen = strlen(needle);
  memset(buf, 'x', len);
  if (nlen < len) memcpy(buf + len - nlen, needle, nlen); /* match at the very end */
  return ds_str_new_len(a, buf, len);
}

static void bench_find(size_t hay_len, const char *label) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_string_t *h = make_haystack(a, hay_len, "NEEDLE");
    ds_string_t *n = ds_str_new(a, "NEEDLE");
    int i;
    ds_bench_start();
    for (i = 0; i < 20000; i++) ds_bench_keep((size_t)(ds_str_find(h, n) + 1));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report(label, 20000);
}

static void bench_find_miss(size_t hay_len) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_string_t *h = make_haystack(a, hay_len, "NEEDLE");
    ds_string_t *n = ds_str_new(a, "ABSENT");
    int i;
    ds_bench_start();
    for (i = 0; i < 20000; i++) ds_bench_keep((size_t)(ds_str_find(h, n) + 1));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("find, 4 KiB haystack, no match", 20000);
}

static void bench_new(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    int i;
    ds_bench_start();
    for (i = 0; i < ITERS; i++) ds_bench_keep(ds_str_new(a, "a moderately sized string value")->length);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_str_new (31 bytes)", ITERS);
}

static void bench_concat(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t *x = ds_str_new(a, "left-hand side ");
    ds_string_t *y = ds_str_new(a, "right-hand side");
    int i;
    ds_bench_start();
    for (i = 0; i < ITERS; i++) ds_bench_keep(ds_str_concat(a, x, y)->length);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_str_concat", ITERS);
}

static void bench_equal(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_string_t *x = ds_str_new(a, "comparison subject string");
    ds_string_t *y = ds_str_new(a, "comparison subject string");
    int i;
    ds_bench_start();
    for (i = 0; i < ITERS; i++) ds_bench_keep(ds_str_equal(x, y) ? 1u : 0u);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_str_equal (matching 25 bytes)", ITERS);
}

static void bench_split(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t *csv = ds_str_new(a, "alpha,beta,gamma,delta,epsilon,zeta,eta,theta,iota,kappa");
    ds_string_t *comma = ds_str_new(a, ",");
    int i;
    ds_bench_start();
    for (i = 0; i < 20000; i++) ds_bench_keep(ds_da_len(ds_str_split(a, csv, comma)));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_str_split (10 fields)", 20000);
}

static void bench_replace(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    ds_string_t *src = ds_str_new(a, "one two one two one two one two one two one two");
    ds_string_t *from = ds_str_new(a, "one");
    ds_string_t *to = ds_str_new(a, "three");
    int i;
    ds_bench_start();
    for (i = 0; i < 20000; i++) ds_bench_keep(ds_str_replace(a, src, from, to)->length);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_str_replace (6 occurrences)", 20000);
}

static void bench_format(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(64u * 1024u * 1024u);
    int i;
    ds_bench_start();
    for (i = 0; i < 50000; i++) ds_bench_keep(ds_str_format(a, "%s-%d-%.2f", "key", i, 1.5)->length);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_str_format (stack-buffer fast path)", 50000);
}

int main(void) {
  ds_bench_banner("string");
  bench_find(16, "find, 16 B haystack (scalar regardless)");
  bench_find(64, "find, 64 B haystack");
  bench_find(4096, "find, 4 KiB haystack, match at end");
  bench_find(65536, "find, 64 KiB haystack, match at end");
  bench_find_miss(4096);
  bench_new();
  bench_concat();
  bench_equal();
  bench_split();
  bench_replace();
  bench_format();
  ds_bench_footer();
  return 0;
}
