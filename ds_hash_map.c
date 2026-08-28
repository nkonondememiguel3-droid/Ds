#include "ds_hash_map.h"

#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "gc.h"

/* ------------------------------------------------------------------ */
/* GC integration                                                      */
/* ------------------------------------------------------------------ */

static void ds_map_entry_mark(ds_node_t node, _ds_arena_t_ *a) {
  _ds_hash_entry_t_ *e = (_ds_hash_entry_t_ *)ds_get_ptr(node);
  if (!e) return;
  if (e->key) ds_gc_push_mark_stack_context(a, ds_tag_ptr(e->key, TYPE_STRING));
  ds_gc_push_mark_stack_context(a, e->value);
  if (e->next) ds_gc_push_mark_stack_context(a, ds_tag_ptr(e->next, TYPE_NODE));
}

static void ds_map_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_hash_map_t *map = (ds_hash_map_t *)ds_get_ptr(node);
  size_t i;
  if (!map || !map->buckets) return;

  for (i = 0; i < map->bucket_count; i++) {
    if (map->buckets[i]) ds_gc_push_mark_stack_context(a, ds_tag_ptr(map->buckets[i], TYPE_NODE));
  }
}

static void ds_map_finalize(void *ptr, _ds_arena_t_ *a) {
  ds_hash_map_t *map = (ds_hash_map_t *)ptr;
  if (map && map->buckets) {
    ds_arena_recycle_raw(a, map->buckets, ds_arena_align_up(map->bucket_count * sizeof(_ds_hash_entry_t_ *)));
    map->buckets = NULL;
    map->bucket_count = 0;
    map->size = 0;
  }
}

const ds_type_descriptor_t ds_map_descriptor = {ds_map_mark, ds_map_finalize};
const ds_type_descriptor_t ds_map_entry_descriptor = {ds_map_entry_mark, NULL};

/* ------------------------------------------------------------------ */
/* Hashing                                                             */
/* ------------------------------------------------------------------ */

/* DJB2. Computed on the full key, folded into the bucket count separately,
 * so a resize does not have to re-walk the key bytes. */
static size_t ds_hash_bytes(const ds_string_t *str) {
  size_t hash = 5381;
  size_t i;
  for (i = 0; i < str->length; i++) hash = ((hash << 5) + hash) + (unsigned char)str->data[i];
  return hash;
}

static size_t ds_hash(const ds_string_t *str, size_t bucket_count) { return ds_hash_bytes(str) % bucket_count; }

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

ds_hash_map_t *ds_map_new(_ds_arena_t_ *a, size_t initial_buckets) {
  ds_hash_map_t *map;
  size_t buckets_bytes;

  if (!a) return NULL;
  if (initial_buckets == 0) initial_buckets = 16;

  map = (ds_hash_map_t *)ds_arena_alloc(a, sizeof(ds_hash_map_t), &ds_map_descriptor);
  map->bucket_count = initial_buckets;
  map->size = 0;

  /* The bucket table is raw storage owned by the map, released by
   * ds_map_finalize. Allocating it as a managed object -- as the original
   * did -- meant the collector swept the table itself on the first cycle. */
  buckets_bytes = ds_arena_align_up(initial_buckets * sizeof(_ds_hash_entry_t_ *));
  map->buckets = (_ds_hash_entry_t_ **)ds_arena_alloc_raw(a, buckets_bytes);

  return map;
}

ds_node_t ds_map_get(const ds_hash_map_t *map, const ds_string_t *key) {
  size_t index;
  const _ds_hash_entry_t_ *curr;

  if (!map || !map->buckets || !key) return ds_make_nil();

  index = ds_hash(key, map->bucket_count);
  for (curr = map->buckets[index]; curr; curr = curr->next) {
    if (ds_str_equal(curr->key, key)) return curr->value;
  }

  return ds_make_nil();
}

bool ds_map_remove(_ds_arena_t_ *a, ds_hash_map_t *map, const ds_string_t *key) {
  size_t index;
  _ds_hash_entry_t_ **curr;

  if (!a || !map || !map->buckets || !key) return false;

  index = ds_hash(key, map->bucket_count);
  curr = &map->buckets[index];

  while (*curr) {
    _ds_hash_entry_t_ *entry = *curr;
    if (ds_str_equal(entry->key, key)) {
      *curr = entry->next;
      entry->next = NULL;
      ds_arena_recycle(a, entry, sizeof(_ds_hash_entry_t_));
      map->size--;
      return true;
    }
    curr = &entry->next;
  }

  return false;
}

void ds_map_resize(_ds_arena_t_ *a, ds_hash_map_t *map, size_t new_bucket_count) {
  size_t new_bytes, old_bytes, i;
  _ds_hash_entry_t_ **new_buckets;
  _ds_hash_entry_t_ **old_buckets;
  size_t old_count;

  if (!a || !map || !map->buckets || new_bucket_count <= map->bucket_count) return;
  if (new_bucket_count > (size_t)-1 / sizeof(_ds_hash_entry_t_ *)) return;

  new_bytes = ds_arena_align_up(new_bucket_count * sizeof(_ds_hash_entry_t_ *));
  new_buckets = (_ds_hash_entry_t_ **)ds_arena_alloc_raw(a, new_bytes);

  old_buckets = map->buckets;
  old_count = map->bucket_count;

  for (i = 0; i < old_count; i++) {
    _ds_hash_entry_t_ *curr = old_buckets[i];
    while (curr) {
      _ds_hash_entry_t_ *next_node = curr->next;
      size_t new_index = ds_hash(curr->key, new_bucket_count);

      curr->next = new_buckets[new_index];
      new_buckets[new_index] = curr;

      curr = next_node;
    }
  }

  map->buckets = new_buckets;
  map->bucket_count = new_bucket_count;

  /* Hand the old table back. The original simply dropped it, so every
   * resize permanently lost bucket_count * sizeof(ptr) bytes of arena. */
  old_bytes = ds_arena_align_up(old_count * sizeof(_ds_hash_entry_t_ *));
  ds_arena_recycle_raw(a, old_buckets, old_bytes);
}

void ds_map_put(_ds_arena_t_ *a, ds_hash_map_t *map, ds_string_t *key, ds_node_t value) {
  size_t index;
  _ds_hash_entry_t_ *curr;
  _ds_hash_entry_t_ *entry;

  if (!a || !map || !map->buckets || !key) return;

  if ((double)map->size > 0.75 * (double)map->bucket_count) ds_map_resize(a, map, map->bucket_count * 2);

  index = ds_hash(key, map->bucket_count);

  for (curr = map->buckets[index]; curr; curr = curr->next) {
    if (ds_str_equal(curr->key, key)) {
      curr->value = value;
      return;
    }
  }

  entry = (_ds_hash_entry_t_ *)ds_arena_alloc(a, sizeof(_ds_hash_entry_t_), &ds_map_entry_descriptor);
  entry->key = key;
  entry->value = value;

  entry->next = map->buckets[index];
  map->buckets[index] = entry;
  map->size++;
}
