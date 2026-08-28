#ifndef ds_hash_map_h
#define ds_hash_map_h

#include "common.h"
#include "ds_arena.h"
#include "ds_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ds_hash_entry_ {
  ds_string_t *key;
  ds_node_t value;
  /* DJB2 of the key, cached at insert. A resize then only has to take it
   * modulo the new bucket count, instead of walking every key's bytes
   * again -- which was 25% of insert time on a map that grows from 4 to
   * 16384 buckets. It also lets lookups reject a mismatch without calling
   * ds_str_equal at all. */
  size_t hash;
  struct _ds_hash_entry_ *next;
} _ds_hash_entry_t_;

typedef struct {
  DS_ALIGNAS(ARENA_ALIGN) _ds_hash_entry_t_ **buckets;
  size_t bucket_count;
  size_t size;
} ds_hash_map_t;

extern const ds_type_descriptor_t ds_map_descriptor;
extern const ds_type_descriptor_t ds_map_entry_descriptor;

ds_hash_map_t *ds_map_new(_ds_arena_t_ *a, size_t initial_buckets);
void ds_map_put(_ds_arena_t_ *a, ds_hash_map_t *map, ds_string_t *key, ds_node_t value);
ds_node_t ds_map_get(const ds_hash_map_t *map, const ds_string_t *key);
bool ds_map_remove(_ds_arena_t_ *a, ds_hash_map_t *map, const ds_string_t *key);
void ds_map_resize(_ds_arena_t_ *a, ds_hash_map_t *map, size_t new_bucket_count);

#ifdef __cplusplus
}
#endif

#endif /* ds_hash_map_h */
