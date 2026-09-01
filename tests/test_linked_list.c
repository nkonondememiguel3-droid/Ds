/* Circular doubly linked list. */

#include "ds_arena.h"
#include "ds_linked_list.h"
#include "ds_test.h"
#include "gc.h"

static bool match_by_value(ds_node_t a, ds_node_t b) { return ds_unpack_int(a) == ds_unpack_int(b); }

static void test_append_prepend(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list = ds_list_new(a);

  SECTION("append and prepend");

  CHECK(list != NULL && list->length == 0 && list->head == NULL, "a new list is empty");

  ds_list_append(a, list, ds_make_int(1));
  CHECK(list->length == 1, "the first append sets the length");
  CHECK(list->head->next == list->head && list->head->prev == list->head,
        "a single node is its own successor and predecessor");

  ds_list_append(a, list, ds_make_int(2));
  ds_list_append(a, list, ds_make_int(3));
  CHECK(list->length == 3, "append counts");
  CHECK(ds_unpack_int(list->head->value) == 1, "head is the first appended");
  CHECK(ds_unpack_int(list->head->prev->value) == 3, "the chain is circular");
  CHECK(list->head->next->next->next == list->head, "three nodes close the ring");

  ds_list_prepend(a, list, ds_make_int(0));
  CHECK(list->length == 4 && ds_unpack_int(list->head->value) == 0, "prepend moves the head");
  CHECK(ds_unpack_int(list->head->prev->value) == 3, "prepend keeps the tail");

  ds_list_append(a, NULL, ds_make_int(9));
  ds_list_prepend(a, NULL, ds_make_int(9));
  CHECK(1, "append and prepend are NULL-safe");

  ds_arena_destroy(a);
}

static void test_find(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list = ds_list_new(a);
  int i;

  SECTION("find");

  for (i = 0; i < 10; i++) ds_list_append(a, list, ds_make_int(i));

  CHECK(ds_list_find(list, ds_make_int(0), NULL) == list->head, "finds the head");
  CHECK(ds_list_find(list, ds_make_int(9), NULL) == list->head->prev, "finds the tail");
  CHECK(ds_list_find(list, ds_make_int(5), NULL) != NULL, "finds an interior node");
  CHECK(ds_list_find(list, ds_make_int(99), NULL) == NULL, "reports a miss without looping forever");
  CHECK(ds_list_find(list, ds_make_int(5), match_by_value) != NULL, "a custom predicate is honoured");
  CHECK(ds_list_find(NULL, ds_make_int(1), NULL) == NULL, "find is NULL-safe");

  ds_arena_destroy(a);
}

static void test_remove(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list = ds_list_new(a);
  ds_list_node_t *found;

  SECTION("remove");

  ds_list_append(a, list, ds_make_int(1));
  ds_list_append(a, list, ds_make_int(2));
  ds_list_append(a, list, ds_make_int(3));

  found = ds_list_find(list, ds_make_int(2), NULL);
  CHECK(ds_list_remove(a, list, found), "removing an interior node succeeds");
  CHECK(list->length == 2, "remove decrements the length");
  CHECK(ds_list_find(list, ds_make_int(2), NULL) == NULL, "the removed value is gone");
  CHECK(list->head->next->next == list->head, "the ring closes over the gap");

  CHECK(ds_list_remove(a, list, list->head), "removing the head succeeds");
  CHECK(ds_unpack_int(list->head->value) == 3, "the head advances");

  /* The old code keyed the last-element case off length == 1 && head ==
   * node, so a node that was its own successor but not the head fell into
   * the general unlink path and left head dangling. */
  CHECK(ds_list_remove(a, list, list->head), "regression: the final node can be removed");
  CHECK(list->length == 0 && list->head == NULL, "an emptied list has a NULL head");

  CHECK(!ds_list_remove(a, list, NULL), "removing NULL fails cleanly");
  CHECK(!ds_list_remove(a, NULL, NULL), "removing from a NULL list fails cleanly");

  ds_arena_destroy(a);
}

static void test_drain_and_refill(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list = ds_list_new(a);
  int i, ok = 1;

  SECTION("drain and refill");

  for (i = 0; i < 500; i++) ds_list_append(a, list, ds_make_int(i));
  while (list->length > 0) ds_list_remove(a, list, list->head);
  CHECK(list->length == 0 && list->head == NULL, "the list drains completely");

  /* Refilling should reuse the recycled nodes rather than growing. */
  for (i = 0; i < 500; i++) ds_list_append(a, list, ds_make_int(i * 2));
  CHECK(list->length == 500, "the list refills");
  CHECK(a->allocs_from_free_list > 0, "refilling reuses recycled nodes");

  {
    ds_list_node_t *curr = list->head;
    for (i = 0; i < 500; i++) {
      if (ds_unpack_int(curr->value) != i * 2) ok = 0;
      curr = curr->next;
    }
    CHECK(ok && curr == list->head, "order is preserved after the refill");
  }

  ds_arena_destroy(a);
}

static void test_gc_interaction(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list = ds_list_new(a);
  ds_node_t root;
  int i;

  SECTION("collection");

  for (i = 0; i < 100; i++) ds_list_append(a, list, ds_make_int(i));
  root = ds_tag_ptr(list, TYPE_NODE);
  ds_gc_register_root(a, &root);

  ds_arena_run_gc(a);
  CHECK(list->length == 100, "a rooted list keeps its nodes");
  CHECK(ds_unpack_int(list->head->prev->value) == 99, "the tail is still reachable");

  ds_gc_unregister_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->live_managed_allocations == 0, "an unrooted list and all its nodes are collected");

  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("linked_list");
  test_append_prepend();
  test_find();
  test_remove();
  test_drain_and_refill();
  test_gc_interaction();
  return ds_test_end();
}
