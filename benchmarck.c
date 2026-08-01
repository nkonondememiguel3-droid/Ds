#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_hash_map.h"
#include "ds_linked_list.h"
#include "gc.h"

// Utilitaire pour mesurer le temps en millisecondes
static double get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

// 1. BENCHMARK : ALLOCATION & RECYCLAGE BRUT DE L'ARÈNE
void bench_arena_and_gc(size_t iterations) {
  printf("Running Arena & GC Benchmark (%zu iterations)...\n", iterations);
  _ds_arena_t_ arena = ds_arena_new(0);

  double start = get_time_ms();

  for (size_t i = 0; i < iterations; i++) {
    // Allocation de blocs de tailles diverses (simulant l'activité d'un programme)
    void *ptr1 = ds_arena_alloc(&arena, 16);
    void *ptr2 = ds_arena_alloc(&arena, 32);

    // Simulation d'une libération immédiate par une structure de données
    /* ds_arena_recycle(&arena, ptr1); */
    /* ds_arena_recycle(&arena, ptr2); */

    // Déclenchement du GC tous les 5000 cycles pour nettoyer les trackers
    if (i % 5000 == 0) {
      ds_arena_run_gc(&arena);
    }
  }

  double end = get_time_ms();
  ds_arena_print_stats(&arena);
  printf("Arena & GC Time: %.2f ms\n\n", end - start);

  ds_arena_destroy(&arena);
  /* ds_gc_destroy(); */
}

// 2. BENCHMARK : TABLEAU DYNAMIQUE CORRIGÉ
void bench_dynamic_array(size_t elements) {
  printf("Running Dynamic Array Benchmark (%zu pushes)...\n", elements);
  _ds_arena_t_ arena = ds_arena_new(0);
  int *array = NULL;

  double start = get_time_ms();

  for (size_t i = 0; i < elements; i++) {
    ds_da_push(&arena, array, (int)i * 2);
  }

  double end = get_time_ms();
  printf("Dynamic Array - Final Length: %zu, Capacity: %zu\n", ds_da_len(array), ds_da_cap(array));
  printf("Dynamic Array Time: %.2f ms\n\n", end - start);

  ds_arena_destroy(&arena);
  /* ds_gc_destroy(); */
}

// 3. BENCHMARK : LISTE CHAÎNÉE CIRCULAIRE CORRIGÉE
void bench_linked_list(size_t elements) {
  printf("Running Linked List Benchmark (%zu elements)...\n", elements);
  _ds_arena_t_ arena = ds_arena_new(0);
  ds_list_t *list = ds_list_new(&arena);

  // PROTECTION : Enregistrement de la racine de la liste
  ds_node_t list_root = ds_tag_ptr(list, TYPE_NODE);
  ds_gc_register_root(&arena, &list_root);

  double start = get_time_ms();

  for (size_t i = 0; i < elements; i++) {
    ds_list_append(&arena, list, ds_make_int((int)i));
  }

  ds_list_node_t *curr = list->head;
  for (size_t i = 0; i < elements / 2; i++) {
    if (!curr) break;
    ds_list_node_t *next_target = curr->next;
    ds_list_remove(&arena, list, curr);
    curr = next_target->next;
  }

  ds_arena_run_gc(&arena);

  double end = get_time_ms();
  printf("Linked List - Final Length: %zu\n", list->length);
  printf("Linked List Time: %.2f ms\n\n", end - start);

  ds_arena_destroy(&arena);
  /* ds_gc_destroy(); */
}

// 4. BENCHMARK : TABLE DE HACHAGE CORRIGÉE
void bench_hash_map(size_t elements) {
  printf("Running Hash Map Benchmark (%zu entries with auto-resize)...\n", elements);
  _ds_arena_t_ arena = ds_arena_new(0);
  ds_hash_map_t *map = ds_map_new(&arena, 4);

  // PROTECTION : Enregistrement de la racine de la Map
  ds_node_t map_root = ds_tag_ptr(map, TYPE_NODE);
  ds_gc_register_root(&arena, &map_root);

  double start = get_time_ms();

  char key_buffer[32];  // CORRIGÉ : Tableau fixe pour snprintf sécurisé
  for (size_t i = 0; i < elements; i++) {
    snprintf(key_buffer, sizeof(key_buffer), "key-id-%zu", i);
    ds_string_t *k = ds_str_new(&arena, key_buffer);
    ds_map_put(&arena, map, k, ds_make_int((int)i * 10));
  }

  snprintf(key_buffer, sizeof(key_buffer), "key-id-%zu", elements - 1);
  ds_string_t *search_key = ds_str_new(&arena, key_buffer);
  ds_node_t result = ds_map_get(map, search_key);

  double end = get_time_ms();

  printf("Hash Map - Final Size: %zu, Buckets Count: %zu\n", map->size, map->bucket_count);
  if (ds_get_type(result) == TYPE_INT) {
    printf("Hash Map - Verification key found with value: %d\n", ds_unpack_int(result));
  }
  printf("Hash Map Time: %.2f ms\n\n", end - start);

  ds_arena_destroy(&arena);
  /* ds_gc_destroy(); */
}

int main() {
  printf("==================================================\n");
  printf("     STARTING FRAMEWORK PERFORMANCE BENCHMARK     \n");
  printf("==================================================\n\n");

  // Ajustez ces valeurs pour augmenter ou réduire la charge du stress-test
  bench_arena_and_gc(100000);  // 100k allocations/recyclages
  bench_linked_list(20000);    // 20k insertions/suppressions de maillons
  bench_hash_map(10000);       // 10k insertions de chaînes et rehashings

  printf("==================================================\n");
  printf("             ALL BENCHMARKS COMPLETED             \n");
  printf("==================================================\n");
  return 0;
}
