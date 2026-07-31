#ifndef ds_hash_map_h
#define ds_hash_map_h

#include "common.h"
#include "ds_string.h"

typedef struct _ds_hash_entry_ {
  ds_string_t *key;
  ds_node_t value;
  struct _ds_hash_entry_ *next;
} _ds_hash_entry_t_;

typedef struct ALIGN16 {
  _ds_hash_entry_t_ **buckets;
  size_t bucket_count;
  size_t size;
} ALIGN16_POST ds_hash_map_t;

extern ds_hash_map_t *ds_map_new(_ds_arena_t_ *a, size_t initial_buckets);
extern void ds_map_put(_ds_arena_t_ *a, ds_hash_map_t *map, ds_string_t *key, ds_node_t value);
extern ds_node_t ds_map_get(const ds_hash_map_t *map, const ds_string_t *key);
extern bool ds_map_remove(_ds_arena_t_ *a, ds_hash_map_t *map, const ds_string_t *key);
extern void ds_map_resize(_ds_arena_t_ *a, ds_hash_map_t *map, size_t new_bucket_count);

#endif  // ds_hash_map_h
