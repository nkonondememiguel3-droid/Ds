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
  CHECK(a->live_managed_allocations == 202, "202 managed objects are live");

  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 2, "the rooted cycle survives, the 200 orphans are swept");

  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 2, "a second collection is idempotent");

  root = ds_make_nil();
  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 0, "breaking the root collects the cycle itself");

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
  CHECK(a->live_managed_allocations == 0, "a raw buffer is neither traced nor swept");

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
  CHECK(ds_gc_state_of(a)->mark_stack_cap <= 2048, "regression: the mark stack stays bounded when tracing a long list");

  ds_gc_unregister_root(a, &root);
  ds_arena_destroy(a);
}

static size_t largest_free(const _ds_arena_t_ *a) {
  const _ds_arena_chunk_t_ *c;
  size_t largest = 0;
  for (c = a->head; c; c = c->next_arena_chunk) {
    const _ds_free_cell_t_ *fc;
    for (fc = c->free_list_head; fc; fc = fc->next) {
      size_t sz = (((const _ds_block_header_t_ *)fc) - 1)->total_size;
      if (sz > largest) largest = sz;
    }
  }
  return largest;
}

static void test_sweep_coalescing(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  int i;
  size_t expected;

  SECTION("sweep coalescing");

  for (i = 0; i < 200; i++) ds_arena_alloc(a, 48, NULL); /* unreachable */
  expected = 200 * (48 + ARENA_ALIGN);

  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 0, "all 200 orphans are swept");

  /* The sweep hands the freed blocks to ds_arena_rebuild_free_lists, which
   * walks each chunk in address order and merges neighbours. The old
   * head-insert scheme swept in pointer-hash order and merged essentially
   * nothing, leaving the free list as a pile of unusable crumbs. */
  CHECK(largest_free(a) >= expected * 9 / 10, "regression: adjacent dead blocks coalesce into one large free block");

  {
    void *big = ds_arena_alloc_raw(a, expected / 2);
    CHECK(big != NULL && a->allocs_from_free_list > 0, "the coalesced region is reusable");
  }

  ds_arena_destroy(a);
}

static void test_collector_attachment(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_node_t root = ds_make_nil();

  SECTION("collector attachment");

  /* The collector keeps no state inside the arena struct: a fresh arena has
   * none, and the first call that needs some allocates it. */
  CHECK(ds_gc_state_of(a) == NULL, "a fresh arena has no collector state");

  ds_gc_register_root(a, &root);
  CHECK(ds_gc_state_of(a) != NULL, "registering a root attaches the collector");
  CHECK(a->collector_destroy != NULL, "the arena is given a teardown hook to call");
  CHECK(ds_gc_state_of(a)->roots != NULL, "the root is recorded in the collector state, not the arena");

  ds_gc_unregister_root(a, &root);
  ds_arena_destroy(a);
}

static void test_collect_without_roots(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  int i;

  SECTION("collection with no roots");

  /* Nothing has ever attached collector state here, so there are no roots
   * and nothing is reachable. The sweep still has to run. */
  for (i = 0; i < 10; i++) ds_arena_alloc(a, 32, &cell_descriptor);
  CHECK(ds_gc_state_of(a) == NULL, "allocating managed blocks does not attach a collector");

  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 0, "with no roots registered, every managed block is garbage");

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
  CHECK(a->live_managed_allocations == 1, "a rooted object survives");

  /* A root points at a variable. If that variable is a local, it has to be
   * deregistered before its frame goes away. */
  ds_gc_unregister_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 0, "deregistering the root makes the object collectable");

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
  test_sweep_coalescing();
  test_collector_attachment();
  test_collect_without_roots();
  test_root_lifecycle();
  return ds_test_end();
}
