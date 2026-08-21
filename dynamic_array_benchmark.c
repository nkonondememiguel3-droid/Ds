#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <time.h>

#include "ds_arena.h"
#include "gc.h"

typedef struct {
  ds_node_t child;
  ds_node_t label;
} ElementNode;

// --- DÉCLARATION DU TYPE DESCRIPTEUR POUR NOTRE CONTAINER ---
void custom_container_mark(ds_node_t node, _ds_arena_t_ *a) {
  void *ptr = ds_get_ptr(node);
  if (!ptr) return;

  ElementNode *n = (ElementNode *)ptr;
  if (n->child) {
    ds_gc_push_mark_stack_context(a, n->child);
  }
  if (n->label) {
    ds_gc_push_mark_stack_context(a, n->label);
  }
}

static ds_type_descriptor_t element_node_descriptor = {.mark = custom_container_mark};

static double get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

int main() {
  printf("==================================================\n");
  printf("  BANC D'ESSAI DES CONTENEURS : GESTION DES CYCLES \n");
  printf("==================================================\n\n");

  _ds_arena_t_ *arena = ds_arena_new(0);

  double start = get_time_ms();

  // Passage du descripteur de type lors de l'allocation managée
  ElementNode *el_a = ARENA_NEW(arena, ElementNode, &element_node_descriptor);
  ElementNode *el_b = ARENA_NEW(arena, ElementNode, &element_node_descriptor);
  void *string_payload = ds_arena_alloc_raw(arena, 64);

  el_a->child = ds_tag_ptr(el_b, TYPE_NODE);
  el_a->label = ds_tag_ptr(string_payload, TYPE_STRING);
  el_b->child = ds_tag_ptr(el_a, TYPE_NODE);

  ds_node_t root = ds_tag_ptr(el_a, TYPE_NODE);
  ds_gc_register_root(arena, &root);

  for (int i = 0; i < 5000; i++) {
    ARENA_NEW(arena, ElementNode, &element_node_descriptor);
  }

  printf("--- PREMIER PASSAGE : Le cycle est référencé par la Racine ---\n");
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  printf("--- SECOND PASSAGE : Rupture de la Racine ---\n");
  root = (ds_node_t)0;
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  double end = get_time_ms();
  printf("Total Execution Time: %.2f ms\n", end - start);

  ds_arena_destroy(arena);
  printf("[Succès] Destruction de l'infrastructure achevée.\n");
  return 0;
}
