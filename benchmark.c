#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_hash_map.h"
#include "ds_linked_list.h"
#include "ds_platform.h"
#include "gc.h"

/* ds_time_ms() replaces the direct clock_gettime(CLOCK_MONOTONIC) call,
 * which does not exist in the MSVC CRT. */

/* 1. ARENA ALLOCATION & RECYCLING */
static void bench_arena_and_gc(size_t iterations) {
  _ds_arena_t_ *arena;
  double start, end;
  size_t i;

  printf("Running Arena & GC benchmark (%zu iterations)...\n", iterations);
  arena = ds_arena_new(0);

  start = ds_time_ms();
  for (i = 0; i < iterations; i++) {
    ds_arena_alloc(arena, 16, NULL);
    ds_arena_alloc(arena, 32, NULL);
    if (i % 5000 == 0) ds_arena_run_gc(arena);
  }
  end = ds_time_ms();

  ds_arena_print_stats(arena);
  printf("Arena & GC time: %.2f ms\n\n", end - start);

  ds_arena_destroy(arena);
}

/* 2. DYNAMIC ARRAY */
static void bench_dynamic_array(size_t elements) {
  _ds_arena_t_ *arena;
  int *array = NULL;
  double start, end;
  size_t i;

  printf("Running dynamic array benchmark (%zu pushes)...\n", elements);
  arena = ds_arena_new(0);

  start = ds_time_ms();
  for (i = 0; i < elements; i++) ds_da_push(arena, array, (int)i * 2);
  end = ds_time_ms();

  printf("Dynamic array - final length: %zu, capacity: %zu\n", ds_da_len(array), ds_da_cap(array));
  printf("Dynamic array time: %.2f ms\n\n", end - start);

  ds_arena_destroy(arena);
}

/* 3. CIRCULAR LINKED LIST */
static void bench_linked_list(size_t elements) {
  _ds_arena_t_ *arena;
  ds_list_t *list;
  ds_node_t list_root;
  ds_list_node_t *curr;
  double start, end;
  size_t i, limit;

  printf("Running linked list benchmark (%zu elements)...\n", elements);
  arena = ds_arena_new(0);
  list = ds_list_new(arena);

  list_root = ds_tag_ptr(list, TYPE_NODE);
  ds_gc_register_root(arena, &list_root);

  start = ds_time_ms();

  for (i = 0; i < elements; i++) ds_list_append(arena, list, ds_make_int((int)i));

  curr = list->head;
  limit = elements / 2;
  for (i = 0; i < limit; i++) {
    ds_list_node_t *next_to_process;
    if (!curr || list->length <= 1) break;
    next_to_process = curr->next->next;
    ds_list_remove(arena, list, curr);
    curr = next_to_process;
  }

  ds_arena_run_gc(arena);
  end = ds_time_ms();

  printf("Linked list - final length: %zu\n", list->length);
  printf("Linked list time: %.2f ms\n\n", end - start);

  /* The root points at a local; drop it before the frame goes away. */
  ds_gc_unregister_root(arena, &list_root);
  ds_arena_destroy(arena);
}

/* 4. HASH MAP */
static void bench_hash_map(size_t elements) {
  _ds_arena_t_ *arena;
  ds_hash_map_t *map;
  ds_node_t map_root;
  ds_string_t *search_key;
  ds_node_t result;
  char key_buffer[32];
  double start, end;
  size_t i;

  printf("Running hash map benchmark (%zu entries with auto-resize)...\n", elements);
  arena = ds_arena_new(0);
  map = ds_map_new(arena, 4);

  map_root = ds_tag_ptr(map, TYPE_NODE);
  ds_gc_register_root(arena, &map_root);

  start = ds_time_ms();

  for (i = 0; i < elements; i++) {
    ds_string_t *k;
    snprintf(key_buffer, sizeof(key_buffer), "key-id-%zu", i);
    k = ds_str_new(arena, key_buffer);
    ds_map_put(arena, map, k, ds_make_int((int)i * 10));
  }

  snprintf(key_buffer, sizeof(key_buffer), "key-id-%zu", elements - 1);
  search_key = ds_str_new(arena, key_buffer);
  result = ds_map_get(map, search_key);

  end = ds_time_ms();

  printf("Hash map - final size: %zu, bucket count: %zu\n", map->size, map->bucket_count);
  if (ds_get_type(result) == TYPE_INT)
    printf("Hash map - verification key found with value: %d\n", ds_unpack_int(result));
  else
    printf("Hash map - VERIFICATION FAILED: key not found\n");
  printf("Hash map time: %.2f ms\n\n", end - start);

  ds_gc_unregister_root(arena, &map_root);
  ds_arena_destroy(arena);
}

int main(void) {
  printf("==================================================\n");
  printf("      STARTING FRAMEWORK PERFORMANCE BENCHMARK     \n");
  printf("==================================================\n");
  printf("AVX2 substring search: %s\n\n", ds_cpu_has_avx2() ? "enabled" : "not available on this CPU");

  bench_arena_and_gc(100000);
  bench_dynamic_array(50000);
  bench_linked_list(20000);
  bench_hash_map(10000);

  printf("==================================================\n");
  printf("              ALL BENCHMARKS COMPLETED            \n");
  printf("==================================================\n");
  return 0;
}
