#include "ds_hash_map.h"

#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_hash_map.h"

// DJB2 algorithm
static size_t ds_hash(const ds_string_t *str, size_t bucket_count) {
  size_t hash = 5381;
  for (size_t i = 0; i < str->length; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)str->data[i];
  }
  return hash % bucket_count;
}

ds_hash_map_t *ds_map_new(_ds_arena_t_ *a, size_t initial_buckets) {
  if (initial_buckets == 0) initial_buckets = 16;

  ds_hash_map_t *map = (ds_hash_map_t *)ds_arena_alloc(a, sizeof(ds_hash_map_t));
  map->bucket_count = initial_buckets;
  map->size = 0;

  size_t buckets_bytes = ds_arena_align_up(initial_buckets * sizeof(_ds_hash_entry_t_ *));
  map->buckets = (_ds_hash_entry_t_ **)ds_arena_alloc(a, buckets_bytes);

  return map;
}

ds_node_t ds_map_get(const ds_hash_map_t *map, const ds_string_t *key) {
  if (!map || !key) return ds_tag_ptr(NULL, TYPE_NIL);

  size_t index = ds_hash(key, map->bucket_count);
  _ds_hash_entry_t_ *curr = map->buckets[index];

  while (curr) {
    if (ds_str_equal(curr->key, key)) {
      return curr->value;
    }
    curr = curr->next;
  }

  return ds_tag_ptr(NULL, TYPE_NIL);
}

bool ds_map_remove(_ds_arena_t_ *a, ds_hash_map_t *map, const ds_string_t *key) {
  if (!map || !key) return false;

  size_t index = ds_hash(key, map->bucket_count);
  _ds_hash_entry_t_ *curr = map->buckets[index];
  _ds_hash_entry_t_ *prev = NULL;

  while (curr) {
    if (ds_str_equal(curr->key, key)) {
      if (prev) {
        prev->next = curr->next;
      } else {
        map->buckets[index] = curr->next;
      }

      ds_arena_recycle(a, curr);
      map->size--;
      return true;
    }
    prev = curr;
    curr = curr->next;
  }

  return false;
}

void ds_map_resize(_ds_arena_t_ *a, ds_hash_map_t *map, size_t new_bucket_count) {
  if (!map || new_bucket_count <= map->bucket_count) return;

  size_t new_bytes = ds_arena_align_up(new_bucket_count * sizeof(_ds_hash_entry_t_ *));
  _ds_hash_entry_t_ **new_buckets = (_ds_hash_entry_t_ **)ds_arena_alloc(a, new_bytes);

  for (size_t i = 0; i < map->bucket_count; i++) {
    _ds_hash_entry_t_ *curr = map->buckets[i];
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
}

void ds_map_put(_ds_arena_t_ *a, ds_hash_map_t *map, ds_string_t *key, ds_node_t value) {
  if (!map || !key) return;

  if ((float)map->size / (float)map->bucket_count > 0.75f) {
    ds_map_resize(a, map, map->bucket_count * 2);
  }

  size_t index = ds_hash(key, map->bucket_count);
  _ds_hash_entry_t_ *curr = map->buckets[index];

  while (curr) {
    if (ds_str_equal(curr->key, key)) {
      curr->value = value;
      return;
    }
    curr = curr->next;
  }

  _ds_hash_entry_t_ *entry = (_ds_hash_entry_t_ *)ds_arena_alloc(a, sizeof(_ds_hash_entry_t_));
  entry->key = key;
  entry->value = value;

  entry->next = map->buckets[index];
  map->buckets[index] = entry;
  map->size++;
}
