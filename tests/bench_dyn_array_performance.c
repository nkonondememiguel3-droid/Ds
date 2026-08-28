#include <stdio.h>
#include <stdlib.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_platform.h"
#include "gc.h"

typedef struct {
  ds_node_t data_payload;
} ManagedObject;

static void managed_object_mark(ds_node_t node, _ds_arena_t_ *a) {
  void *ptr = ds_get_ptr(node);
  if (!ptr) return;
  ManagedObject *obj = (ManagedObject *)ptr;
  ds_gc_push_mark_stack_context(a, obj->data_payload);
}

static const ds_type_descriptor_t managed_object_descriptor = {.mark = managed_object_mark, .finalize = NULL};

int main(void) {
  printf("==================================================\n");
  printf("     CONTAINER BENCH: AUTOMATIC GC MANAGEMENT      \n");
  printf("==================================================\n\n");

  _ds_arena_t_ *arena = ds_arena_new(16 * 1024 * 1024);

  ds_array_t *raw_vector = ds_array_new(arena, sizeof(int), 1024, 0);
  double start_raw = ds_time_ms();
  for (int i = 0; i < 10000000; i++) {
    ds_array_push(arena, raw_vector, &i);
  }
  double end_raw = ds_time_ms();
  printf("Test 1 done. Raw push time: %.2f ms\n\n", end_raw - start_raw);
  ds_array_free_raw(arena, raw_vector);

  ds_node_t managed_vector_root = 0;
  ds_gc_register_root(arena, &managed_vector_root);

  ds_array_t *managed_vector =
      ds_array_new(arena, sizeof(ds_node_t), 1024, DS_ARRAY_HEADER_MANAGED | DS_ARRAY_ELEMENTS_ARE_GC);
  managed_vector_root = ds_tag_ptr(managed_vector, TYPE_NODE);

  double start_managed = ds_time_ms();
  for (int i = 0; i < 500000; i++) {
    ManagedObject *obj = (ManagedObject *)ds_arena_alloc(arena, sizeof(ManagedObject), &managed_object_descriptor);
    obj->data_payload = ds_make_int(i * 10);

    ds_node_t wrapped_node = ds_tag_ptr(obj, TYPE_NODE);
    ds_array_push(arena, managed_vector, &wrapped_node);
  }
  double end_managed = ds_time_ms();
  printf("Test 2 done. Managed insert time: %.2f ms\n", end_managed - start_managed);

  printf("\n[GC] Collection with the array still rooted (it must survive):");
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  printf("\n[GC] Clearing the root (container and children become unreachable)...");
  managed_vector_root = (ds_node_t)0;

  printf("\n[GC] Full collection (sweep plus finalizers):");
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  ds_gc_unregister_root(arena, &managed_vector_root);
  ds_arena_destroy(arena);
  printf("==================================================\n");
  printf("                BENCHMARK COMPLETE                 \n");
  printf("==================================================\n");
  return 0;
}
