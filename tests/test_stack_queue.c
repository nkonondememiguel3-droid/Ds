/* Stack and queue, both layered over the circular list. */

#include "ds_arena.h"
#include "ds_stack_queue.h"
#include "ds_test.h"
#include "gc.h"

static void test_stack(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_stack_t *st = ds_stack_new(a);
  int i, ok = 1;

  SECTION("stack");

  CHECK(st != NULL && ds_stack_size(st) == 0, "a new stack is empty");
  CHECK(ds_get_type(ds_stack_pop(a, st)) == TYPE_NIL, "popping an empty stack yields NIL");
  CHECK(ds_get_type(ds_stack_peek(st)) == TYPE_NIL, "peeking an empty stack yields NIL");

  ds_stack_push(a, st, ds_make_int(10));
  ds_stack_push(a, st, ds_make_int(20));
  ds_stack_push(a, st, ds_make_int(30));
  CHECK(ds_stack_size(st) == 3, "size counts pushes");
  CHECK(ds_unpack_int(ds_stack_peek(st)) == 30, "peek returns the top without removing it");
  CHECK(ds_stack_size(st) == 3, "peek does not pop");

  CHECK(ds_unpack_int(ds_stack_pop(a, st)) == 30, "pop is LIFO");
  CHECK(ds_unpack_int(ds_stack_pop(a, st)) == 20, "pop is LIFO again");
  CHECK(ds_stack_size(st) == 1, "the stack shrinks");
  CHECK(ds_unpack_int(ds_stack_pop(a, st)) == 10, "the last element pops");
  CHECK(ds_stack_size(st) == 0, "the stack drains");
  CHECK(ds_get_type(ds_stack_pop(a, st)) == TYPE_NIL, "popping a drained stack yields NIL");

  /* The old ds_stack_size dereferenced stack->list unconditionally. */
  CHECK(ds_stack_size(NULL) == 0, "regression: size is NULL-safe");
  CHECK(ds_get_type(ds_stack_peek(NULL)) == TYPE_NIL, "peek is NULL-safe");
  ds_stack_push(a, NULL, ds_make_int(1));
  CHECK(1, "push is NULL-safe");

  for (i = 0; i < 1000; i++) ds_stack_push(a, st, ds_make_int(i));
  for (i = 999; i >= 0; i--)
    if (ds_unpack_int(ds_stack_pop(a, st)) != i) ok = 0;
  CHECK(ok, "a thousand values come back in reverse order");

  ds_arena_destroy(a);
}

static void test_queue(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_queue_t *q = ds_queue_new(a);
  int i, ok = 1;

  SECTION("queue");

  CHECK(q != NULL && ds_queue_size(q) == 0, "a new queue is empty");
  CHECK(ds_get_type(ds_queue_dequeue(a, q)) == TYPE_NIL, "dequeuing an empty queue yields NIL");
  CHECK(ds_get_type(ds_queue_peek(q)) == TYPE_NIL, "peeking an empty queue yields NIL");

  ds_queue_enqueue(a, q, ds_make_int(1));
  ds_queue_enqueue(a, q, ds_make_int(2));
  ds_queue_enqueue(a, q, ds_make_int(3));
  CHECK(ds_queue_size(q) == 3, "size counts enqueues");
  CHECK(ds_unpack_int(ds_queue_peek(q)) == 1, "peek returns the front");

  CHECK(ds_unpack_int(ds_queue_dequeue(a, q)) == 1, "dequeue is FIFO");
  CHECK(ds_unpack_int(ds_queue_dequeue(a, q)) == 2, "dequeue is FIFO again");
  CHECK(ds_unpack_int(ds_queue_dequeue(a, q)) == 3, "the last element dequeues");
  CHECK(ds_queue_size(q) == 0, "the queue drains");

  CHECK(ds_queue_size(NULL) == 0, "size is NULL-safe");
  CHECK(ds_get_type(ds_queue_peek(NULL)) == TYPE_NIL, "peek is NULL-safe");

  for (i = 0; i < 1000; i++) ds_queue_enqueue(a, q, ds_make_int(i));
  for (i = 0; i < 1000; i++)
    if (ds_unpack_int(ds_queue_dequeue(a, q)) != i) ok = 0;
  CHECK(ok, "a thousand values come back in order");

  ds_arena_destroy(a);
}

static void test_interleaved(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_queue_t *q = ds_queue_new(a);
  int i, ok = 1;

  SECTION("interleaved traffic");

  /* Alternating enqueue and dequeue keeps the list short and exercises the
   * recycle-then-reuse path on every iteration. */
  for (i = 0; i < 2000; i++) {
    ds_queue_enqueue(a, q, ds_make_int(i));
    if (i % 2 == 1) {
      ds_node_t v = ds_queue_dequeue(a, q);
      if (ds_unpack_int(v) != i / 2) ok = 0;
    }
  }
  CHECK(ok, "interleaved enqueue and dequeue preserve order");
  CHECK(ds_queue_size(q) == 1000, "half the values remain queued");
  CHECK(a->allocs_from_free_list > 0, "dequeued nodes are recycled and reused");

  ds_arena_destroy(a);
}

static void test_gc_interaction(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_stack_t *st = ds_stack_new(a);
  ds_queue_t *q = ds_queue_new(a);
  ds_node_t st_root, q_root;
  int i;

  SECTION("collection");

  for (i = 0; i < 100; i++) {
    ds_stack_push(a, st, ds_make_int(i));
    ds_queue_enqueue(a, q, ds_make_int(i));
  }

  st_root = ds_tag_ptr(st, TYPE_NODE);
  q_root = ds_tag_ptr(q, TYPE_NODE);
  ds_gc_register_root(a, &st_root);
  ds_gc_register_root(a, &q_root);

  ds_arena_run_gc(a);

  /* Both wrappers own their backing list only by pointer; without a mark
   * callback the list was collected out from under them. */
  CHECK(ds_stack_size(st) == 100, "regression: a rooted stack keeps its backing list");
  CHECK(ds_queue_size(q) == 100, "regression: a rooted queue keeps its backing list");
  CHECK(ds_unpack_int(ds_stack_peek(st)) == 99, "stack contents are intact");
  CHECK(ds_unpack_int(ds_queue_peek(q)) == 0, "queue contents are intact");

  ds_gc_unregister_root(a, &st_root);
  ds_gc_unregister_root(a, &q_root);
  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("stack_queue");
  test_stack();
  test_queue();
  test_interleaved();
  test_gc_interaction();
  return ds_test_end();
}
