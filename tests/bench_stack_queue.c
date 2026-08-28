/* Stack and queue throughput, including the steady-state churn case. */

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_stack_queue.h"

#define N 200000

static void bench_stack_push(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_stack_t *s = ds_stack_new(a);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_stack_push(a, s, ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("stack push", N);
}

static void bench_stack_pop(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_stack_t *s = ds_stack_new(a);
    int i;
    for (i = 0; i < N; i++) ds_stack_push(a, s, ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)ds_unpack_int(ds_stack_pop(a, s)));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("stack pop", N);
}

/* Push/pop in lockstep: every pop frees a node the next push takes back,
 * so this measures the recycle-then-reuse round trip. */
static void bench_stack_churn(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_stack_t *s = ds_stack_new(a);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) {
      ds_stack_push(a, s, ds_make_int(i));
      ds_bench_keep((size_t)ds_unpack_int(ds_stack_pop(a, s)));
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("stack push+pop in lockstep", N);
}

static void bench_queue_enqueue(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_queue_t *q = ds_queue_new(a);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_queue_enqueue(a, q, ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("queue enqueue", N);
}

static void bench_queue_dequeue(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_queue_t *q = ds_queue_new(a);
    int i;
    for (i = 0; i < N; i++) ds_queue_enqueue(a, q, ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)ds_unpack_int(ds_queue_dequeue(a, q)));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("queue dequeue", N);
}

/* A bounded working set, which is how a queue is normally used. */
static void bench_queue_steady_state(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_queue_t *q = ds_queue_new(a);
    int i;
    for (i = 0; i < 1000; i++) ds_queue_enqueue(a, q, ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < N; i++) {
      ds_queue_enqueue(a, q, ds_make_int(i));
      ds_bench_keep((size_t)ds_unpack_int(ds_queue_dequeue(a, q)));
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("queue steady state, 1000 deep", N);
}

int main(void) {
  ds_bench_banner("stack_queue");
  bench_stack_push();
  bench_stack_pop();
  bench_stack_churn();
  bench_queue_enqueue();
  bench_queue_dequeue();
  bench_queue_steady_state();
  ds_bench_footer();
  return 0;
}
