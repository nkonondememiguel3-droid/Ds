#include "ds_graph.h"

#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"

ds_graph_t *ds_graph_new(_ds_arena_t_ *a) {
  ds_graph_t *graph = (ds_graph_t *)ds_arena_alloc(a, sizeof(ds_graph_t));
  graph->vertices = ds_list_new(a);
  return graph;
}

ds_vertex_t *ds_graph_add_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id, ds_node_t value) {
  if (!graph || !id) return NULL;

  ds_vertex_t *existing = ds_graph_find_vertex(graph, id);
  if (existing) return existing;

  ds_vertex_t *vertex = (ds_vertex_t *)ds_arena_alloc(a, sizeof(ds_vertex_t));
  vertex->id = ds_str_new(a, id);
  vertex->value = value;
  vertex->neighbors = NULL;

  ds_list_append(a, graph->vertices, ds_tag_ptr(vertex, TYPE_NODE));
  return vertex;
}

void ds_graph_add_edge(_ds_arena_t_ *a, ds_vertex_t *from, ds_vertex_t *to) {
  if (!from || !to) return;

  ds_da_push(a, from->neighbors, to);
}

ds_vertex_t *ds_graph_find_vertex(const ds_graph_t *graph, const char *id) {
  if (!graph || !graph->vertices || graph->vertices->head == NULL || graph->vertices->length == 0) {
    return NULL;
  }

  ds_list_node_t *curr = graph->vertices->head;
  for (size_t i = 0; i < graph->vertices->length; i++) {
    if (!curr) break;

    ds_vertex_t *v = (ds_vertex_t *)ds_get_ptr(curr->value);
    if (v && v->id) {
      // DÉPAQUETAGE REQUIS : v->id est un pointeur étiqueté, on doit utiliser ds_get_ptr
      ds_string_t *actual_id = (ds_string_t *)ds_get_ptr((ds_node_t)v->id);
      if (actual_id && actual_id->data) {
        if (strcmp(actual_id->data, id) == 0) {
          return v;
        }
      }
    }
    curr = curr->next;
  }
  return NULL;
}

bool ds_graph_remove_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id) {
  if (!graph || !graph->vertices || graph->vertices->head == NULL || !id) return false;

  ds_list_node_t *curr = graph->vertices->head;
  size_t initial_length = graph->vertices->length;

  for (size_t i = 0; i < initial_length; i++) {
    if (!curr) break;

    ds_vertex_t *v = (ds_vertex_t *)ds_get_ptr(curr->value);
    if (v && v->id) {
      // DÉPAQUETAGE REQUIS ICI AUSSI pour éviter le SIGSEGV à l'adresse 0x8
      ds_string_t *actual_id = (ds_string_t *)ds_get_ptr((ds_node_t)v->id);
      if (actual_id && actual_id->data) {
        if (strcmp(actual_id->data, id) == 0) {
          // 1. Retirer le maillon du sommet de la liste circulaire du graphe
          ds_list_remove(a, graph->vertices, curr);

          // 2. Recycler le tableau dynamique de voisins s'il a été alloué
          if (v->neighbors) {
            ds_arena_recycle(a, ds_da_hdr(v->neighbors));
          }

          // 3. Recycler le sommet lui-même
          ds_arena_recycle(a, v);
          return true;
        }
      }
    }
    curr = curr->next;
  }
  return false;
}

extern void gc_mark_node(ds_node_t node);

// Logique de marquage spécifique à la topologie du graphe
void ds_graph_gc_mark_extension(ds_node_t node) {
  void *ptr = ds_get_ptr(node);
  if (!ptr) return;

  ds_vertex_t *v = (ds_vertex_t *)ptr;

  // 1. Marquer la structure de la String ID pour éviter son balayage
  if (v->id) {
    gc_mark_node(ds_tag_ptr(v->id, TYPE_NODE));
  }

  // 2. Parcourir et marquer récursivement les sommets voisins du tableau dynamique
  if (v->neighbors) {
    size_t neighbor_count = ds_da_len(v->neighbors);
    for (size_t i = 0; i < neighbor_count; i++) {
      if (v->neighbors[i]) {
        gc_mark_node(ds_tag_ptr(v->neighbors[i], TYPE_NODE));
      }
    }
  }
}
