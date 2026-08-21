#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "gc.h"

static double get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

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

int main() {
  printf("==================================================\n");
  printf("  BANC D'ESSAI DES CONTENEURS : AUTOMATISATION GC  \n");
  printf("==================================================\n\n");

  _ds_arena_t_ *arena = ds_arena_new(16 * 1024 * 1024);

  ds_array_t *raw_vector = ds_array_new(arena, sizeof(int), 1024, 0);
  double start_raw = get_time_ms();
  for (int i = 0; i < 10000000; i++) {
    ds_array_push(arena, raw_vector, &i);
  }
  double end_raw = get_time_ms();
  printf("Test 1 achevé. Temps d'exécution brute : %.2f ms\n\n", end_raw - start_raw);
  ds_array_free_raw(arena, raw_vector);

  ds_node_t managed_vector_root = 0;
  ds_gc_register_root(arena, &managed_vector_root);

  ds_array_t *managed_vector =
      ds_array_new(arena, sizeof(ds_node_t), 1024, DS_ARRAY_HEADER_MANAGED | DS_ARRAY_ELEMENTS_ARE_GC);
  managed_vector_root = ds_tag_ptr(managed_vector, TYPE_NODE);

  double start_managed = get_time_ms();
  for (int i = 0; i < 500000; i++) {
    ManagedObject *obj = (ManagedObject *)ds_arena_alloc(arena, sizeof(ManagedObject), &managed_object_descriptor);
    obj->data_payload = ds_make_int(i * 10);

    ds_node_t wrapped_node = ds_tag_ptr(obj, TYPE_NODE);
    ds_array_push(arena, managed_vector, &wrapped_node);
  }
  double end_managed = get_time_ms();
  printf("Test 2 : Insertions terminées. Temps d'exécution : %.2f ms\n", end_managed - start_managed);

  printf("\n[GC] Lancement de la collecte de protection (Le tableau survit) :");
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  printf("\n[GC] Rupture de la Racine (Le conteneur et les enfants deviennent orphelins)...");
  managed_vector_root = (ds_node_t)0;

  printf("\n[GC] Lancement de la collecte générale (Purge et finalisation atomique) :");
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  ds_arena_destroy(arena);
  printf("==================================================\n");
  printf("   BENCHMARK TERMINÉ : FLUIDITÉ ABSOLUE RETROUVÉE  \n");
  printf("==================================================\n");
  return 0;
}
