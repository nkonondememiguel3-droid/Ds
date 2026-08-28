/* The mark-and-sweep collector: roots, cycles, descriptors, reachability. */

#include <stdio.h>

#include "ds_arena.h"
#include "ds_hash_map.h"
#include "ds_linked_list.h"
#include "ds_string.h"
#include "ds_test.h"
#include "gc.h"

typedef struct {
  ds_node_t child;
} Cell;

static void cell_mark(ds_node_t node, _ds_arena_t_ *a) {
  Cell *c = (Cell *)ds_get_ptr(node);
  if (c) ds_gc_push_mark_stack_context(a, c->child);
}

static int finalizer_calls = 0;

static void cell_finalize(void *ptr, _ds_arena_t_ *a) {
  (void)ptr;
  (void)a;
  finalizer_calls++;
}

static const ds_type_descriptor_t cell_descriptor = {cell_mark, NULL};
static const ds_type_descriptor_t cell_final_descriptor = {cell_mark, cell_finalize};

static void test_null_arena(void) {
  SECTION("null arena");
  ds_arena_run_gc(NULL);
  ds_gc_register_root(NULL, NULL);
  ds_gc_unregister_root(NULL, NULL);
  CHECK(1, "regression: the collector checks the arena before dereferencing it");
}

static void test_cycles(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  Cell *x, *y;
  ds_node_t root;
  int i;

  SECTION("cycles and roots");

  /* x -> y -> x is unreachable but cyclic: refcounting would leak it. */
  x = ARENA_NEW(a, Cell, &cell_descriptor);
  y = ARENA_NEW(a, Cell, &cell_descriptor);
  x->child = ds_tag_ptr(y, TYPE_NODE);
  y->child = ds_tag_ptr(x, TYPE_NODE);

  root = ds_tag_ptr(x, TYPE_NODE);
  ds_gc_register_root(a, &root);

  for (i = 0; i < 200; i++) ARENA_NEW(a, Cell, &cell_descriptor);
  CHECK(a->gc_live_allocations == 202, "202 managed objects are live");

  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 2, "the rooted cycle survives, the 200 orphans are swept");

  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 2, "a second collection is idempotent");

  root = ds_make_nil();
  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 0, "breaking the root collects the cycle itself");

  ds_gc_unregister_root(a, &root);
  ds_arena_destroy(a);
}

static void test_finalizers(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  int i;

  SECTION("finalizers");

  finalizer_calls = 0;
  for (i = 0; i < 50; i++) ARENA_NEW(a, Cell, &cell_final_descriptor);
  ds_arena_run_gc(a);
  CHECK(finalizer_calls == 50, "every swept object gets its finalizer");

  finalizer_calls = 0;
  ds_arena_run_gc(a);
  CHECK(finalizer_calls == 0, "a finalizer runs exactly once");

  ds_arena_destroy(a);
}

static void test_raw_is_not_traced(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_node_t root;

  SECTION("raw payloads");

  /* A user value may legitimately hold a tagged pointer to a raw buffer
   * (the arena's own benchmarks do exactly this). The collector must skip
   * it rather than treat the bytes in front of it as a header. */
  root = ds_tag_ptr(ds_arena_alloc_raw(a, 128), TYPE_STRING);
  ds_gc_register_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 0, "a raw buffer is neither traced nor swept");

  ds_gc_unregister_root(a, &root);
  ds_arena_destroy(a);
}

static void test_reachability_through_containers(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list;
  ds_hash_map_t *map;
  ds_node_t list_root, map_root;
  int i, ok = 1;

  SECTION("reachability through containers");

  list = ds_list_new(a);
  list_root = ds_tag_ptr(list, TYPE_NODE);
  ds_gc_register_root(a, &list_root);
  for (i = 0; i < 50; i++) ds_list_append(a, list, ds_make_int(i));

  map = ds_map_new(a, 8);
  map_root = ds_tag_ptr(map, TYPE_NODE);
  ds_gc_register_root(a, &map_root);
  for (i = 0; i < 50; i++) {
    char k[16];
    snprintf(k, sizeof(k), "k%d", i);
    ds_map_put(a, map, ds_str_new(a, k), ds_make_int(i));
  }

  ds_arena_run_gc(a);

  /* Before the type descriptors existed the container objects survived
   * (they were roots) but every node, entry, key and character buffer they
   * owned was swept out from under them on the first collection. */
  CHECK(list->length == 50, "the list still reports its length after a collection");

  for (i = 0; i < 50; i++) {
    char k[16];
    ds_node_t v;
    snprintf(k, sizeof(k), "k%d", i);
    v = ds_map_get(map, ds_str_new(a, k));
    if (ds_get_type(v) != TYPE_INT || ds_unpack_int(v) != i) ok = 0;
  }
  CHECK(ok, "regression: map entries and their string keys survive a collection");

  {
    ds_list_node_t *curr = list->head;
    int seen = 0;
    ok = 1;
    do {
      if (ds_unpack_int(curr->value) != seen) ok = 0;
      seen++;
      curr = curr->next;
    } while (curr != list->head && seen < 100);
    CHECK(ok && seen == 50, "regression: list nodes survive a collection and stay in order");
  }

  ds_gc_unregister_root(a, &list_root);
  ds_gc_unregister_root(a, &map_root);
  ds_arena_destroy(a);
}

static void test_mark_stack_bound(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list = ds_list_new(a);
  ds_node_t root;
  int i;

  SECTION("mark stack bound");

  for (i = 0; i < 20000; i++) ds_list_append(a, list, ds_make_int(i));
  root = ds_tag_ptr(list, TYPE_NODE);
  ds_gc_register_root(a, &root);
  ds_arena_run_gc(a);

  CHECK(list->length == 20000, "the list survives the collection intact");
  /* Following `next` alone reaches every node on a circular chain; pushing
   * `prev` too grew this stack to 32768 entries for the same 20k nodes. */
  CHECK(a->gc_mark_stack_cap <= 2048, "regression: the mark stack stays bounded when tracing a long list");

  ds_gc_unregister_root(a, &root);
  ds_arena_destroy(a);
}

static void test_root_lifecycle(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_node_t root;
  Cell *c;

  SECTION("root lifecycle");

  c = ARENA_NEW(a, Cell, &cell_descriptor);
  root = ds_tag_ptr(c, TYPE_NODE);
  ds_gc_register_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 1, "a rooted object survives");

  /* A root points at a variable. If that variable is a local, it has to be
   * deregistered before its frame goes away. */
  ds_gc_unregister_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 0, "deregistering the root makes the object collectable");

  ds_gc_unregister_root(a, &root);
  CHECK(1, "deregistering twice is harmless");

  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("gc");
  test_null_arena();
  test_cycles();
  test_finalizers();
  test_raw_is_not_traced();
  test_reachability_through_containers();
  test_mark_stack_bound();
  test_root_lifecycle();
  return ds_test_end();
}
