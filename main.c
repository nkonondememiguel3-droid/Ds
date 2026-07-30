#include <stdio.h>

#include "ds_arena.h"
#include "ds_graph.h"
#include "gc.h"

// Déclaration du callback implémenté dans ds_graph.c
extern void ds_graph_gc_mark_extension(ds_node_t node);

int main() {
  // ÉTAPE INDISPENSABLE : Branchement de l'extension de marquage du graphe sur le GC
  ds_gc_set_mark_extension(ds_graph_gc_mark_extension);

  _ds_arena_t_ arena = ds_arena_new(0);
  ds_graph_t *mon_graphe = ds_graph_new(&arena);

  ds_node_t graph_root = ds_tag_ptr(mon_graphe, TYPE_NODE);
  ds_gc_register_root(&arena, &graph_root);

  ds_vertex_t *vA = ds_graph_add_vertex(&arena, mon_graphe, "A", ds_make_int(100));
  ds_vertex_t *vB = ds_graph_add_vertex(&arena, mon_graphe, "B", ds_make_int(200));
  ds_vertex_t *vC = ds_graph_add_vertex(&arena, mon_graphe, "C", ds_make_int(300));

  ds_graph_add_edge(&arena, vA, vB);
  ds_graph_add_edge(&arena, vB, vA);
  ds_graph_add_edge(&arena, vB, vC);

  printf("Graphe orienté circulaire initialisé. Nombre de sommets : %zu\n", mon_graphe->vertices->length);

  printf("Suppression du point d'entrée 'A' du graphe...\n");
  ds_graph_remove_vertex(&arena, mon_graphe, "A");

  ds_arena_run_gc(&arena);
  ds_arena_print_stats(&arena);

  ds_arena_destroy(&arena);
  ds_gc_destroy();

  printf("Test des Graphes Orientés Circulaires achevé avec succès !\n");
  return 0;
}
