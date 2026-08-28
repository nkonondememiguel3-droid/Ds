/* Both dynamic array styles: ds_array_t and the ds_da_* bare-pointer API. */

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_test.h"
#include "gc.h"

static void test_struct_array(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_array_t *arr;
  int i, ok = 1;

  SECTION("ds_array_t");

  arr = ds_array_new(a, sizeof(int), 4, 0);
  CHECK(arr != NULL && ds_array_len(arr) == 0, "a new array starts empty");
  CHECK(ds_array_cap(arr) == 4, "the requested capacity is honoured");

  for (i = 0; i < 1000; i++) ds_array_push(a, arr, &i);
  CHECK(ds_array_len(arr) == 1000, "it grows on demand");

  for (i = 0; i < 1000; i++) {
    int *slot = (int *)ds_array_get(arr, (size_t)i);
    if (!slot || *slot != i) ok = 0;
  }
  CHECK(ok, "contents survive reallocation");
  CHECK(ds_array_get(arr, 1000) == NULL, "ds_array_get bounds-checks");
  CHECK(ds_array_get(NULL, 0) == NULL, "ds_array_get is NULL-safe");
  CHECK(ds_array_len((ds_array_t *)NULL) == 0, "length of a NULL array is zero");

  ds_array_free_raw(a, arr);

  CHECK(ds_array_new(a, 0, 8, 0) == NULL, "a zero element size is rejected");

  ds_arena_destroy(a);
}

static void test_managed_array(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_array_t *arr;
  ds_node_t root;
  int i;

  SECTION("GC-managed ds_array_t");

  arr = ds_array_new(a, sizeof(ds_node_t), 8, DS_ARRAY_HEADER_MANAGED | DS_ARRAY_ELEMENTS_ARE_GC);
  root = ds_tag_ptr(arr, TYPE_NODE);
  ds_gc_register_root(a, &root);

  for (i = 0; i < 500; i++) ds_array_push_node(a, arr, ds_make_int(i));
  CHECK(ds_array_len(arr) == 500, "push_node grows the array");

  ds_arena_run_gc(a);
  CHECK(ds_array_len(arr) == 500, "a rooted managed array survives collection");
  CHECK(ds_unpack_int(((ds_node_t *)arr->data)[499]) == 499, "its elements survive too");

  /* A managed header belongs to the collector; freeing it by hand would
   * leave the collector with a stale reference. */
  ds_array_free_raw(a, arr);
  CHECK(arr->data != NULL, "ds_array_free_raw refuses to free a managed header");

  ds_gc_unregister_root(a, &root);
  ds_arena_destroy(a);
}

static void test_bare_pointer_array(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  int *xs = NULL;
  int i, ok = 1;

  SECTION("ds_da_* API");

  /* This whole API was referenced by four source files and defined
   * nowhere, which is why those modules were commented out of the build. */
  CHECK(ds_da_len(xs) == 0, "a NULL array has length zero");
  CHECK(ds_da_cap(xs) == 0, "a NULL array has no capacity");

  for (i = 0; i < 1000; i++) ds_da_push(a, xs, i * 3);
  CHECK(ds_da_len(xs) == 1000, "push grows it");
  CHECK(ds_da_cap(xs) >= 1000, "capacity keeps up");

  for (i = 0; i < 1000; i++)
    if (xs[i] != i * 3) ok = 0;
  CHECK(ok, "contents survive reallocation");

  CHECK((((uintptr_t)xs) & (ARENA_ALIGN - 1)) == 0, "the payload stays 16-byte aligned, so elements remain taggable");

  CHECK(ds_da_pop(xs) == 999 * 3, "pop returns the last element");
  CHECK(ds_da_len(xs) == 999, "pop shrinks the array");

  ds_da_clear(xs);
  CHECK(ds_da_len(xs) == 0, "clear resets the length");
  CHECK(ds_da_cap(xs) >= 1000, "clear keeps the capacity");

  ds_da_free(a, xs);
  CHECK(xs == NULL, "free clears the handle");

  ds_arena_destroy(a);
}

static void test_da_reserve(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  double *xs = NULL;

  SECTION("ds_da_reserve");

  ds_da_reserve(a, xs, 4096);
  CHECK(ds_da_cap(xs) >= 4096, "reserve grows the capacity");
  CHECK(ds_da_len(xs) == 0, "reserve does not change the length");

  ds_da_push(a, xs, 1.5);
  CHECK(ds_da_len(xs) == 1 && xs[0] == 1.5, "the reserved array still works");

  ds_da_free(a, xs);
  ds_arena_destroy(a);
}

static void test_da_of_pointers(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  void **ptrs = NULL;
  int i, ok = 1;

  SECTION("ds_da_ of pointers");

  for (i = 0; i < 100; i++) ds_da_push(a, ptrs, ds_arena_alloc_raw(a, 32));
  CHECK(ds_da_len(ptrs) == 100, "pointer elements push correctly");

  for (i = 0; i < 100; i++)
    if (!ptrs[i] || (((uintptr_t)ptrs[i]) & (ARENA_ALIGN - 1)) != 0) ok = 0;
  CHECK(ok, "the stored pointers are intact and aligned");

  ds_da_free(a, ptrs);
  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("dyn_array");
  test_struct_array();
  test_managed_array();
  test_bare_pointer_array();
  test_da_reserve();
  test_da_of_pointers();
  return ds_test_end();
}
