/* Both array styles: append throughput, indexing, growth from empty. */

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_dyn_array.h"
#include "gc.h"

#define N 1000000

static void bench_struct_push(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_array_t *v = ds_array_new(a, sizeof(int), 1024, 0);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_array_push(a, v, &i);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_array_t push (int)", N);
}

static void bench_da_push(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    int *xs = NULL;
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_da_push(a, xs, i);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_da_push (int, grows from NULL)", N);
}

static void bench_da_push_reserved(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    int *xs = NULL;
    int i;
    ds_da_reserve(a, xs, N);
    ds_bench_start();
    for (i = 0; i < N; i++) ds_da_push(a, xs, i);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_da_push (pre-reserved, no growth)", N);
}

static void bench_struct_get(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_array_t *v = ds_array_new(a, sizeof(int), N, 0);
    size_t i;
    for (i = 0; i < N; i++) ds_array_push(a, v, &i);
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t) * (int *)ds_array_get(v, i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_array_get (bounds-checked read)", N);
}

static void bench_da_index(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    int *xs = NULL;
    int i;
    ds_da_reserve(a, xs, N);
    for (i = 0; i < N; i++) ds_da_push(a, xs, i);
    ds_bench_start();
    for (i = 0; i < N; i++) ds_bench_keep((size_t)xs[i]);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("ds_da_ direct index (plain pointer)", N);
}

/* Managed elements: every push also creates a traced object, and the whole
 * container has to survive a collection. */
static void bench_managed_elements(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_array_t *v;
    ds_node_t root = 0;
    int i;
    ds_gc_register_root(a, &root);
    v = ds_array_new(a, sizeof(ds_node_t), 1024, DS_ARRAY_HEADER_MANAGED | DS_ARRAY_ELEMENTS_ARE_GC);
    root = ds_tag_ptr(v, TYPE_NODE);
    ds_bench_start();
    for (i = 0; i < 500000; i++) ds_array_push_node(a, v, ds_make_int(i));
    ds_bench_stop();
    ds_gc_unregister_root(a, &root);
    ds_arena_destroy(a);
  }
  ds_bench_report("500k pushes into a GC-managed array", 500000);
}

int main(void) {
  ds_bench_banner("dyn_array");
  bench_struct_push();
  bench_da_push();
  bench_da_push_reserved();
  bench_struct_get();
  bench_da_index();
  bench_managed_elements();
  ds_bench_footer();
  return 0;
}
