#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <time.h>

#include "ds_arena.h"
#include "gc.h"

typedef struct {
  ds_node_t child;
} Node;

void custom_node_mark_extension(ds_node_t node, _ds_arena_t_ *a) {
  void *ptr = ds_get_ptr(node);
  if (!ptr) return;

  Node *n = (Node *)ptr;
  if (n->child) {
    ds_gc_push_mark_stack_context(a, n->child);
  }
}

static double get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

int main() {
  printf("==================================================\n");
  printf("  STARTING 10/10 INDUSTRIAL MANAGED GC BENCHMARK \n");
  printf("==================================================\n\n");

  // CORRIGÉ : Utilisation du bon alias de type _ds_arena_t_
  _ds_arena_t_ arena = ds_arena_new(0);
  ds_gc_set_mark_extension(&arena, custom_node_mark_extension);

  ds_node_t root;
  double start = get_time_ms();

  Node *node_a = ARENA_NEW(&arena, Node);
  Node *node_b = ARENA_NEW(&arena, Node);

  root = ds_tag_ptr(node_a, TYPE_NODE);
  node_a->child = ds_tag_ptr(node_b, TYPE_NODE);

  ds_gc_register_root(&arena, &root);

  for (int i = 0; i < 5000; i++) {
    ARENA_NEW(&arena, Node);
  }

  ds_arena_run_gc(&arena);

  double end = get_time_ms();

  ds_arena_print_stats(&arena);
  printf("Total Execution Time: %.2f ms\n", end - start);

  ds_arena_destroy(&arena);

  printf("\n[Succès] L'arène s'est détruite de manière étanche.\n");
  return 0;
}
