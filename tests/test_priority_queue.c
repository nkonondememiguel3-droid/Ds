/* Binary min-heap over immediate integer nodes. */

#include <stdlib.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_priority_queue.h"
#include "ds_test.h"
#include "gc.h"

static void test_basics(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_priority_queue_t *pq = ds_pq_new(a);

  SECTION("basics");

  CHECK(pq != NULL && ds_pq_size(pq) == 0, "a new heap is empty");
  CHECK(ds_get_type(ds_pq_peek(pq)) == TYPE_NIL, "peeking an empty heap yields NIL");
  CHECK(ds_get_type(ds_pq_pop(a, pq)) == TYPE_NIL, "popping an empty heap yields NIL");
  CHECK(ds_pq_size(NULL) == 0, "size is NULL-safe");
  CHECK(ds_get_type(ds_pq_peek(NULL)) == TYPE_NIL, "peek is NULL-safe");

  ds_pq_push(a, pq, ds_make_int(5));
  CHECK(ds_pq_size(pq) == 1 && ds_unpack_int(ds_pq_peek(pq)) == 5, "a single element is the minimum");

  ds_pq_push(a, pq, ds_make_int(3));
  CHECK(ds_unpack_int(ds_pq_peek(pq)) == 3, "a smaller element sifts to the root");

  ds_pq_push(a, pq, ds_make_int(9));
  CHECK(ds_unpack_int(ds_pq_peek(pq)) == 3, "a larger element does not");

  CHECK(ds_unpack_int(ds_pq_pop(a, pq)) == 3, "pop returns the minimum");
  CHECK(ds_unpack_int(ds_pq_peek(pq)) == 5, "the next minimum surfaces");
  CHECK(ds_pq_size(pq) == 2, "pop shrinks the heap");

  ds_arena_destroy(a);
}

static void test_ordering(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_priority_queue_t *pq = ds_pq_new(a);
  static const int input[] = {5, 3, 9, 1, 7, 1, 8, 2, 6, 4};
  int prev = -1, ok = 1;
  size_t i;

  SECTION("ordering");

  for (i = 0; i < sizeof(input) / sizeof(input[0]); i++) ds_pq_push(a, pq, ds_make_int(input[i]));
  CHECK(ds_pq_size(pq) == 10, "all ten values are stored");
  CHECK(ds_unpack_int(ds_pq_peek(pq)) == 1, "the minimum is at the root");

  /* The old ds_pq_pop wrote heap[0] = ds_da_pop(heap) -- one expression
   * with an unsequenced side effect on the length, so which element
   * survived at the root depended on evaluation order. */
  for (i = 0; i < 10; i++) {
    int v = ds_unpack_int(ds_pq_pop(a, pq));
    if (v < prev) ok = 0;
    prev = v;
  }
  CHECK(ok, "regression: pop yields a non-decreasing sequence");
  CHECK(ds_pq_size(pq) == 0, "the heap drains completely");
  CHECK(ds_get_type(ds_pq_pop(a, pq)) == TYPE_NIL, "popping a drained heap yields NIL");

  ds_arena_destroy(a);
}

static void test_duplicates_and_negatives(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_priority_queue_t *pq = ds_pq_new(a);
  int i, prev, ok = 1;

  SECTION("duplicates and negatives");

  for (i = 0; i < 20; i++) ds_pq_push(a, pq, ds_make_int(7));
  CHECK(ds_pq_size(pq) == 20, "duplicates all fit");
  for (i = 0; i < 20; i++)
    if (ds_unpack_int(ds_pq_pop(a, pq)) != 7) ok = 0;
  CHECK(ok, "duplicates all come back");

  for (i = 10; i >= -10; i--) ds_pq_push(a, pq, ds_make_int(i));
  prev = -1000;
  ok = 1;
  for (i = 0; i < 21; i++) {
    int v = ds_unpack_int(ds_pq_pop(a, pq));
    if (v < prev) ok = 0;
    prev = v;
  }
  CHECK(ok, "negative priorities order correctly");

  ds_arena_destroy(a);
}

static void test_large_random(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_priority_queue_t *pq = ds_pq_new(a);
  int i, prev = -1, ok = 1;
  unsigned seed = 12345u;

  SECTION("large randomised heap");

  for (i = 0; i < 20000; i++) {
    seed = seed * 1103515245u + 12345u;
    ds_pq_push(a, pq, ds_make_int((int)((seed >> 16) & 0x7FFF)));
  }
  CHECK(ds_pq_size(pq) == 20000, "20k values are stored");

  for (i = 0; i < 20000; i++) {
    int v = ds_unpack_int(ds_pq_pop(a, pq));
    if (v < prev) ok = 0;
    prev = v;
  }
  CHECK(ok, "20k randomised values come out sorted");
  CHECK(ds_pq_size(pq) == 0, "the heap drains");

  ds_arena_destroy(a);
}

static void test_gc_interaction(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_priority_queue_t *pq = ds_pq_new(a);
  ds_node_t root;
  int i;

  SECTION("collection");

  for (i = 100; i > 0; i--) ds_pq_push(a, pq, ds_make_int(i));
  root = ds_tag_ptr(pq, TYPE_NODE);
  ds_gc_register_root(a, &root);

  ds_arena_run_gc(a);
  CHECK(ds_pq_size(pq) == 100, "a rooted heap keeps its backing array");
  CHECK(ds_unpack_int(ds_pq_peek(pq)) == 1, "the root element is intact");

  ds_gc_unregister_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 0, "an unrooted heap is collected and its array finalized");

  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("priority_queue");
  test_basics();
  test_ordering();
  test_duplicates_and_negatives();
  test_large_random();
  test_gc_interaction();
  return ds_test_end();
}
