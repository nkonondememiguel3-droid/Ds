/* Binary min-heap: push, pop, and the heapsort round trip. */

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_priority_queue.h"

#define N 200000

static unsigned rng_state;
static int next_rand(void) {
  rng_state = rng_state * 1103515245u + 12345u;
  return (int)((rng_state >> 16) & 0x7FFF);
}

/* Ascending input never sifts up past the first comparison: the best case. */
static void bench_push_ascending(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_priority_queue_t *pq = ds_pq_new(a);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_pq_push(a, pq, ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("push, ascending input (no sift-up)", N);
}

/* Descending input sifts every element to the root: the worst case. */
static void bench_push_descending(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_priority_queue_t *pq = ds_pq_new(a);
    int i;
    ds_bench_start();
    for (i = N; i > 0; i--) ds_pq_push(a, pq, ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("push, descending input (full sift-up)", N);
}

static void bench_push_random(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_priority_queue_t *pq = ds_pq_new(a);
    int i;
    rng_state = 12345u;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_pq_push(a, pq, ds_make_int(next_rand()));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("push, random input", N);
}

static void bench_pop(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_priority_queue_t *pq = ds_pq_new(a);
    int i;
    rng_state = 999u;
    for (i = 0; i < N; i++) ds_pq_push(a, pq, ds_make_int(next_rand()));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)ds_unpack_int(ds_pq_pop(a, pq)));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("pop, draining a random heap", N);
}

static void bench_peek(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_priority_queue_t *pq = ds_pq_new(a);
    int i;
    for (i = 0; i < 1000; i++) ds_pq_push(a, pq, ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)ds_unpack_int(ds_pq_peek(pq)));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("peek", N);
}

/* The scheduler pattern: keep the heap at a fixed depth. */
static void bench_steady_state(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_priority_queue_t *pq = ds_pq_new(a);
    int i;
    rng_state = 4242u;
    for (i = 0; i < 10000; i++) ds_pq_push(a, pq, ds_make_int(next_rand()));
    ds_bench_start();
    for (i = 0; i < N; i++) {
      ds_pq_push(a, pq, ds_make_int(next_rand()));
      ds_bench_keep((size_t)ds_unpack_int(ds_pq_pop(a, pq)));
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("steady state, 10k deep (push+pop)", N);
}

int main(void) {
  ds_bench_banner("priority_queue");
  bench_push_ascending();
  bench_push_descending();
  bench_push_random();
  bench_pop();
  bench_peek();
  bench_steady_state();
  ds_bench_footer();
  return 0;
}
