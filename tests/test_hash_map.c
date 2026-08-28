/* String-keyed chaining hash map with automatic resize. */

#include <stdio.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_hash_map.h"
#include "ds_test.h"
#include "gc.h"

static void test_basics(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_hash_map_t *map = ds_map_new(a, 16);
  ds_string_t *k = ds_str_new(a, "alpha");

  SECTION("basics");

  CHECK(map != NULL && map->size == 0, "a new map is empty");
  CHECK(map->bucket_count == 16, "the requested bucket count is honoured");
  CHECK(ds_map_new(a, 0)->bucket_count > 0, "a zero bucket count falls back to a default");

  CHECK(ds_get_type(ds_map_get(map, k)) == TYPE_NIL, "a missing key yields NIL");

  ds_map_put(a, map, k, ds_make_int(1));
  CHECK(map->size == 1, "put inserts");
  CHECK(ds_unpack_int(ds_map_get(map, k)) == 1, "get finds it");
  CHECK(ds_unpack_int(ds_map_get(map, ds_str_new(a, "alpha"))) == 1, "lookup is by value, not identity");

  ds_map_put(a, map, ds_str_new(a, "alpha"), ds_make_int(2));
  CHECK(map->size == 1, "overwriting a key does not grow the map");
  CHECK(ds_unpack_int(ds_map_get(map, k)) == 2, "the overwrite takes effect");

  CHECK(ds_get_type(ds_map_get(NULL, k)) == TYPE_NIL, "get is NULL-safe");
  CHECK(ds_get_type(ds_map_get(map, NULL)) == TYPE_NIL, "a NULL key yields NIL");
  ds_map_put(a, NULL, k, ds_make_int(1));
  ds_map_put(a, map, NULL, ds_make_int(1));
  CHECK(map->size == 1, "put ignores NULL arguments");

  ds_arena_destroy(a);
}

static void test_growth(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_hash_map_t *map = ds_map_new(a, 4);
  char key[32];
  int i, ok = 1;

  SECTION("growth and rehashing");

  for (i = 0; i < 500; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    ds_map_put(a, map, ds_str_new(a, key), ds_make_int(i * 2));
  }
  CHECK(map->size == 500, "every key is stored");
  CHECK(map->bucket_count > 4, "the map resized itself");

  for (i = 0; i < 500; i++) {
    ds_node_t v;
    snprintf(key, sizeof(key), "k%d", i);
    v = ds_map_get(map, ds_str_new(a, key));
    if (ds_get_type(v) != TYPE_INT || ds_unpack_int(v) != i * 2) ok = 0;
  }
  CHECK(ok, "regression: every key survives the chain of resizes");

  ds_arena_destroy(a);
}

static void test_hash_cache(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_hash_map_t *map = ds_map_new(a, 4);
  char key[48];
  int i, ok = 1;

  SECTION("cached key hashes");

  /* Each entry carries its own hash so a resize can rebucket it without
   * re-walking the key. If a cached value ever disagreed with its key the
   * entry would land in the wrong bucket and the lookup would miss. */
  for (i = 0; i < 600; i++) {
    snprintf(key, sizeof(key), "a-somewhat-longer-cached-key-%d", i);
    ds_map_put(a, map, ds_str_new(a, key), ds_make_int(i));
  }
  CHECK(map->bucket_count >= 1024, "the map grew through many resizes");

  for (i = 0; i < 600; i++) {
    ds_node_t v;
    snprintf(key, sizeof(key), "a-somewhat-longer-cached-key-%d", i);
    v = ds_map_get(map, ds_str_new(a, key));
    if (ds_get_type(v) != TYPE_INT || ds_unpack_int(v) != i) ok = 0;
  }
  CHECK(ok, "cached hashes stay consistent with their keys across every resize");

  ds_arena_destroy(a);
}

static void test_remove(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_hash_map_t *map = ds_map_new(a, 8);
  char key[32];
  int i;

  SECTION("remove");

  for (i = 0; i < 100; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    ds_map_put(a, map, ds_str_new(a, key), ds_make_int(i));
  }

  CHECK(ds_map_remove(a, map, ds_str_new(a, "k7")), "remove succeeds");
  CHECK(map->size == 99, "remove decrements the size");
  CHECK(ds_get_type(ds_map_get(map, ds_str_new(a, "k7"))) == TYPE_NIL, "the removed key is gone");
  CHECK(!ds_map_remove(a, map, ds_str_new(a, "k7")), "removing twice fails cleanly");
  CHECK(!ds_map_remove(a, map, ds_str_new(a, "never-there")), "removing an absent key fails cleanly");
  CHECK(!ds_map_remove(a, NULL, ds_str_new(a, "k1")), "remove is NULL-safe");

  CHECK(ds_unpack_int(ds_map_get(map, ds_str_new(a, "k6"))) == 6, "neighbouring keys are untouched");
  CHECK(ds_unpack_int(ds_map_get(map, ds_str_new(a, "k8"))) == 8, "and on the other side too");

  for (i = 0; i < 100; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    ds_map_remove(a, map, ds_str_new(a, key));
  }
  CHECK(map->size == 0, "the map empties completely");

  ds_arena_destroy(a);
}

static void test_collisions(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  /* One bucket forces every key onto the same chain. */
  ds_hash_map_t *map = ds_map_new(a, 1);
  char key[32];
  int i, ok = 1;

  SECTION("collision chains");

  for (i = 0; i < 200; i++) {
    snprintf(key, sizeof(key), "collide-%d", i);
    ds_map_put(a, map, ds_str_new(a, key), ds_make_int(i));
  }
  for (i = 0; i < 200; i++) {
    ds_node_t v;
    snprintf(key, sizeof(key), "collide-%d", i);
    v = ds_map_get(map, ds_str_new(a, key));
    if (ds_unpack_int(v) != i) ok = 0;
  }
  CHECK(ok, "long collision chains resolve correctly");

  CHECK(ds_map_remove(a, map, ds_str_new(a, "collide-100")), "a mid-chain entry can be removed");
  CHECK(ds_unpack_int(ds_map_get(map, ds_str_new(a, "collide-101"))) == 101, "the chain stays intact");

  ds_arena_destroy(a);
}

static void test_gc_interaction(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_hash_map_t *map = ds_map_new(a, 8);
  ds_node_t root;
  char key[32];
  int i, ok = 1;

  SECTION("collection");

  for (i = 0; i < 200; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    ds_map_put(a, map, ds_str_new(a, key), ds_make_int(i));
  }
  root = ds_tag_ptr(map, TYPE_NODE);
  ds_gc_register_root(a, &root);

  ds_arena_run_gc(a);
  for (i = 0; i < 200; i++) {
    ds_node_t v;
    snprintf(key, sizeof(key), "k%d", i);
    v = ds_map_get(map, ds_str_new(a, key));
    if (ds_get_type(v) != TYPE_INT || ds_unpack_int(v) != i) ok = 0;
  }
  CHECK(ok, "regression: entries, keys and the bucket table all survive a collection");
  CHECK(map->size == 200, "the size is unchanged");

  ds_gc_unregister_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 0, "an unrooted map is collected entirely");

  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("hash_map");
  test_basics();
  test_growth();
  test_hash_cache();
  test_remove();
  test_collisions();
  test_gc_interaction();
  return ds_test_end();
}
