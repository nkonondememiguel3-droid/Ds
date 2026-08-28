/* Collector cost: marking, sweeping, finalizers, cycle handling. */

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_linked_list.h"
#include "gc.h"

typedef struct {
  ds_node_t child;
  ds_node_t label;
} Node2;

static void node2_mark(ds_node_t node, _ds_arena_t_ *a) {
  Node2 *n = (Node2 *)ds_get_ptr(node);
  if (!n) return;
  ds_gc_push_mark_stack_context(a, n->child);
  ds_gc_push_mark_stack_context(a, n->label);
}

static int finalizer_calls;
static void node2_finalize(void *p, _ds_arena_t_ *a) {
  (void)p;
  (void)a;
  finalizer_calls++;
}

static const ds_type_descriptor_t node2_desc = {node2_mark, NULL};
static const ds_type_descriptor_t node2_final_desc = {node2_mark, node2_finalize};

/* One collection with everything dead: pure sweep cost. */
static void bench_sweep_all_dead(size_t live) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    size_t i;
    for (i = 0; i < live; i++) ARENA_NEW(a, Node2, &node2_desc);
    ds_bench_start();
    ds_arena_run_gc(a);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("sweep, everything unreachable", live);
}

/* One collection with everything reachable: mark cost plus a live sweep. */
static void bench_mark_all_live(size_t live) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_list_t *list = ds_list_new(a);
    ds_node_t root;
    size_t i;
    for (i = 0; i < live; i++) ds_list_append(a, list, ds_make_int((int)i));
    root = ds_tag_ptr(list, TYPE_NODE);
    ds_gc_register_root(a, &root);
    ds_bench_start();
    ds_arena_run_gc(a);
    ds_bench_stop();
    ds_gc_unregister_root(a, &root);
    ds_arena_destroy(a);
  }
  ds_bench_report("mark + sweep, everything reachable", live);
}

/* Finalizer dispatch on the sweep path. */
static void bench_finalizers(size_t live) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    size_t i;
    finalizer_calls = 0;
    for (i = 0; i < live; i++) ARENA_NEW(a, Node2, &node2_final_desc);
    ds_bench_start();
    ds_arena_run_gc(a);
    ds_bench_stop();
    ds_bench_keep((size_t)finalizer_calls);
    ds_arena_destroy(a);
  }
  ds_bench_report("sweep with a finalizer per object", live);
}

/* Steady-state allocation interleaved with periodic collection. */
static void bench_alloc_with_gc(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    int i;
    ds_bench_start();
    for (i = 0; i < 100000; i++) {
      ds_arena_alloc(a, 16, NULL);
      ds_arena_alloc(a, 32, NULL);
      if (i % 5000 == 0) ds_arena_run_gc(a);
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("200k managed allocs, collecting every 5k", 200000);
}

/* Unreachable A<->B cycles: the case a refcount cannot reclaim. */
static void bench_cycles(size_t pairs) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    size_t i;
    for (i = 0; i < pairs; i++) {
      Node2 *x = ARENA_NEW(a, Node2, &node2_desc);
      Node2 *y = ARENA_NEW(a, Node2, &node2_desc);
      x->child = ds_tag_ptr(y, TYPE_NODE);
      y->child = ds_tag_ptr(x, TYPE_NODE);
    }
    ds_bench_start();
    ds_arena_run_gc(a);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("collect unreachable A<->B cycles", pairs * 2);
}

int main(void) {
  ds_bench_banner("gc");
  bench_sweep_all_dead(50000);
  bench_sweep_all_dead(500000);
  bench_mark_all_live(50000);
  bench_mark_all_live(500000);
  bench_finalizers(50000);
  bench_cycles(50000);
  bench_alloc_with_gc();
  ds_bench_footer();
  return 0;
}
