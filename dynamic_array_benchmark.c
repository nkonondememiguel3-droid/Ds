#include <stdio.h>

#include "ds_arena.h"
#include "ds_platform.h"
#include "gc.h"

typedef struct {
  ds_node_t child;
  ds_node_t label;
} ElementNode;

/* Type descriptor for our container type. */
static void custom_container_mark(ds_node_t node, _ds_arena_t_ *a) {
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

static const ds_type_descriptor_t element_node_descriptor = {custom_container_mark, NULL};

int main(void) {
  printf("==================================================\n");
  printf("   CONTAINER BENCH: REFERENCE CYCLE HANDLING      \n");
  printf("==================================================\n\n");

  _ds_arena_t_ *arena = ds_arena_new(0);

  double start = ds_time_ms();

  /* The type descriptor is supplied at managed-allocation time. */
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

  printf("--- PASS 1: the cycle is still reachable from the root ---\n");
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  printf("--- PASS 2: the root is cleared ---\n");
  root = (ds_node_t)0;
  ds_arena_run_gc(arena);
  ds_arena_print_stats(arena);

  double end = ds_time_ms();
  printf("Total Execution Time: %.2f ms\n", end - start);

  ds_gc_unregister_root(arena, &root);
  ds_arena_destroy(arena);
  printf("[OK] Arena torn down cleanly.\n");
  return 0;
}
