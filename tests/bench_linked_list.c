/* Circular doubly linked list: append, prepend, traverse, find, remove. */

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_linked_list.h"
#include "gc.h"

#define N 200000

static void bench_append(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_list_t *l = ds_list_new(a);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_list_append(a, l, ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("append", N);
}

static void bench_prepend(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_list_t *l = ds_list_new(a);
    int i;
    ds_bench_start();
    for (i = 0; i < N; i++) ds_list_prepend(a, l, ds_make_int(i));
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("prepend", N);
}

static void bench_traverse(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_list_t *l = ds_list_new(a);
    ds_list_node_t *curr;
    int i;
    for (i = 0; i < N; i++) ds_list_append(a, l, ds_make_int(i));
    ds_bench_start();
    curr = l->head;
    for (i = 0; i < N; i++) {
      ds_bench_keep((size_t)ds_unpack_int(curr->value));
      curr = curr->next;
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("full traversal (pointer chase)", N);
}

/* Worst case for a linear search: the target is the last node visited. */
static void bench_find_worst(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_list_t *l = ds_list_new(a);
    int i;
    for (i = 0; i < 2000; i++) ds_list_append(a, l, ds_make_int(i));
    ds_bench_start();
    for (i = 0; i < 500; i++) ds_bench_keep(ds_list_find(l, ds_make_int(1999), NULL) ? 1u : 0u);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("find, miss until the last of 2000 nodes", 500);
}

static void bench_remove(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_list_t *l = ds_list_new(a);
    int i;
    for (i = 0; i < N; i++) ds_list_append(a, l, ds_make_int(i));
    ds_bench_start();
    while (l->length > 0) ds_list_remove(a, l, l->head);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("remove from the head until empty", N);
}

/* Build, tear half of it down, then collect: the churn pattern the arena's
 * free list exists for. */
static void bench_churn_and_collect(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(0);
    ds_list_t *l = ds_list_new(a);
    ds_node_t root;
    ds_list_node_t *curr;
    int i;

    ds_bench_start();
    for (i = 0; i < 20000; i++) ds_list_append(a, l, ds_make_int(i));
    root = ds_tag_ptr(l, TYPE_NODE);
    ds_gc_register_root(a, &root);

    curr = l->head;
    for (i = 0; i < 10000 && l->length > 1; i++) {
      ds_list_node_t *next = curr->next->next;
      ds_list_remove(a, l, curr);
      curr = next;
    }
    ds_arena_run_gc(a);
    ds_bench_stop();

    ds_gc_unregister_root(a, &root);
    ds_arena_destroy(a);
  }
  ds_bench_report("20k appends, 10k removals, one collection", 30000);
}

int main(void) {
  ds_bench_banner("linked_list");
  bench_append();
  bench_prepend();
  bench_traverse();
  bench_find_worst();
  bench_remove();
  bench_churn_and_collect();
  ds_bench_footer();
  return 0;
}
