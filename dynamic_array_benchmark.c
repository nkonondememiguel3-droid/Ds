#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ds_arena.h"
#include "ds_linked_list.h"
#include "gc.h"

static double get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

int main() {
  printf("==================================================\n");
  printf(" STARTING AIRTIGHT ARCHITECTURE TESTING SUITE    \n");
  printf("==================================================\n\n");

  _ds_arena_t_ arena = ds_arena_new(0);
  ds_list_t *list = ds_list_new(&arena);
  ds_node_t list_root = ds_tag_ptr(list, TYPE_NODE);
  ds_gc_register_root(&arena, &list_root);

  double start = get_time_ms();

  // Allocation de blocs hétérogènes pour valider le First-Fit et la restitution de taille
  for (int i = 0; i < 5000; i++) {
    ds_list_append(&arena, list, ds_make_int(i));
    // Allocation de chaînes fantômes de tailles intermédiaires (128 octets)
    ds_arena_alloc(&arena, 128);
  }

  // Lancement du GC : les chaînes fantômes de 128 octets n'étant pas rattachées à la racine,
  // elles vont être balayées et restituées à la Free-list avec leur vraie taille de 128 octets !
  ds_arena_run_gc(&arena);

  // Tentative de réallocation immédiate de structures de 128 octets.
  // L'allocateur First-Fit doit intercepter ces trous de 128 octets au lieu de grow l'arène.
  for (int i = 0; i < 2000; i++) {
    ds_arena_alloc(&arena, 128);
  }

  double end = get_time_ms();

  ds_arena_print_stats(&arena);
  printf("Airtight Core Bench Time: %.2f ms\n", end - start);

  ds_arena_destroy(&arena);
  return 0;
}
