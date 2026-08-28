/* Tagged-pointer encoding: the ds_node_t representation in common.h. */

#include "common.h"
#include "ds_arena.h"
#include "ds_test.h"

static void test_immediates(void) {
  SECTION("immediate values");

  CHECK(sizeof(ds_node_t) == 8, "ds_node_t is a fixed 64-bit word on every target");

  CHECK(ds_get_type(ds_make_int(42)) == TYPE_INT, "int keeps its tag");
  CHECK(ds_unpack_int(ds_make_int(42)) == 42, "int round-trips");
  CHECK(ds_unpack_int(ds_make_int(0)) == 0, "zero round-trips");

  /* The original shifted a signed value left, which is undefined for
   * negatives, and stored the result in a uintptr_t that dropped the top
   * four bits on a 32-bit build. */
  CHECK(ds_unpack_int(ds_make_int(-7)) == -7, "regression: negative int round-trips");
  CHECK(ds_unpack_int(ds_make_int(2147483647)) == 2147483647, "regression: INT_MAX round-trips");
  CHECK(ds_unpack_int(ds_make_int(-2147483647 - 1)) == -2147483647 - 1, "regression: INT_MIN round-trips");

  CHECK(ds_get_type(ds_make_float(3.5f)) == TYPE_FLOAT, "float keeps its tag");
  CHECK(ds_unpack_float(ds_make_float(3.5f)) == 3.5f, "float round-trips");
  CHECK(ds_unpack_float(ds_make_float(0.0f)) == 0.0f, "zero float round-trips");
  CHECK(ds_unpack_float(ds_make_float(-1.0e30f)) == -1.0e30f, "regression: float with high mantissa bits round-trips");

  CHECK(ds_get_type(ds_make_bool(true)) == TYPE_BOOL && ds_unpack_bool(ds_make_bool(true)), "true round-trips");
  CHECK(!ds_unpack_bool(ds_make_bool(false)), "false round-trips");

  CHECK(ds_get_type(ds_make_nil()) == TYPE_NIL, "nil carries the nil tag");
  CHECK(ds_get_ptr(ds_make_nil()) == NULL, "nil has no pointer");
}

static void test_tagged_pointers(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  int i;
  int ok = 1;

  SECTION("tagged pointers");

  /* Tagging is only defined for ARENA_ALIGN-aligned addresses, which is
   * exactly what the arena guarantees. */
  for (i = 0; i < 200; i++) {
    void *p = ds_arena_alloc_raw(a, (size_t)(i % 61) + 1);
    ds_node_t n = ds_tag_ptr(p, TYPE_NODE);
    ds_node_t s = ds_tag_ptr(p, TYPE_STRING);
    if (ds_get_ptr(n) != p || ds_get_type(n) != TYPE_NODE) ok = 0;
    if (ds_get_ptr(s) != p || ds_get_type(s) != TYPE_STRING) ok = 0;
  }
  CHECK(ok, "every tag round-trips through an arena pointer without losing address bits");

  CHECK(ds_get_ptr(ds_tag_ptr(NULL, TYPE_NODE)) == NULL, "tagging NULL yields NULL");

  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("common (tagged pointers)");
  test_immediates();
  test_tagged_pointers();
  return ds_test_end();
}
