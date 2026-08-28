#include "ds_graph.h"

#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "gc.h"

/* ------------------------------------------------------------------ */
/* GC integration                                                      */
/* ------------------------------------------------------------------ */

/*
 * The original defined ds_graph_gc_mark_extension() which called a
 * gc_mark_node() that does not exist anywhere in the project, was declared
 * in no header, took no arena argument, and was never wired into any type
 * descriptor -- so graph vertices were never traced at all. This is the
 * same idea, expressed against the real collector API.
 */

static void ds_vertex_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_vertex_t *v = (ds_vertex_t *)ds_get_ptr(node);
  size_t i, n;

  if (!v) return;

  if (v->id) ds_gc_push_mark_stack_context(a, ds_tag_ptr(v->id, TYPE_STRING));
  ds_gc_push_mark_stack_context(a, v->value);

  n = ds_da_len(v->neighbors);
  for (i = 0; i < n; i++) {
    if (v->neighbors[i]) ds_gc_push_mark_stack_context(a, ds_tag_ptr(v->neighbors[i], TYPE_NODE));
  }
}

static void ds_vertex_finalize(void *ptr, _ds_arena_t_ *a) {
  ds_vertex_t *v = (ds_vertex_t *)ptr;
  if (v && v->neighbors) {
    ds__da_free(a, v->neighbors);
    v->neighbors = NULL;
  }
}

static void ds_graph_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_graph_t *g = (ds_graph_t *)ds_get_ptr(node);
  if (g && g->vertices) ds_gc_push_mark_stack_context(a, ds_tag_ptr(g->vertices, TYPE_NODE));
}

const ds_type_descriptor_t ds_graph_descriptor = {ds_graph_mark, NULL};
const ds_type_descriptor_t ds_vertex_descriptor = {ds_vertex_mark, ds_vertex_finalize};

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

ds_graph_t *ds_graph_new(_ds_arena_t_ *a) {
  ds_graph_t *graph;
  if (!a) return NULL;

  graph = (ds_graph_t *)ds_arena_alloc(a, sizeof(ds_graph_t), &ds_graph_descriptor);
  graph->vertices = ds_list_new(a);
  return graph;
}

ds_vertex_t *ds_graph_add_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id, ds_node_t value) {
  ds_vertex_t *existing;
  ds_vertex_t *vertex;

  if (!a || !graph || !id) return NULL;

  existing = ds_graph_find_vertex(graph, id);
  if (existing) return existing;

  vertex = (ds_vertex_t *)ds_arena_alloc(a, sizeof(ds_vertex_t), &ds_vertex_descriptor);
  vertex->id = ds_str_new(a, id);
  vertex->value = value;
  vertex->neighbors = NULL;

  ds_list_append(a, graph->vertices, ds_tag_ptr(vertex, TYPE_NODE));
  return vertex;
}

void ds_graph_add_edge(_ds_arena_t_ *a, ds_vertex_t *from, ds_vertex_t *to) {
  if (!a || !from || !to) return;
  ds_da_push(a, from->neighbors, to);
}

ds_vertex_t *ds_graph_find_vertex(const ds_graph_t *graph, const char *id) {
  ds_list_node_t *curr;
  size_t i;

  if (!graph || !graph->vertices || !graph->vertices->head || graph->vertices->length == 0 || !id) return NULL;

  curr = graph->vertices->head;
  for (i = 0; i < graph->vertices->length; i++) {
    ds_vertex_t *v;

    if (!curr) break;

    v = (ds_vertex_t *)ds_get_ptr(curr->value);
    /* v->id is a plain ds_string_t*, not a tagged node. The original ran it
     * through ds_get_ptr(), masking off the low four bits of a perfectly
     * good pointer -- harmless only as long as every allocation happened to
     * be 16-byte aligned, which it was not. */
    if (v && v->id && v->id->data && strcmp(v->id->data, id) == 0) return v;

    curr = curr->next;
  }
  return NULL;
}

bool ds_graph_remove_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id) {
  ds_list_node_t *curr;
  size_t initial_length, i;

  if (!a || !graph || !graph->vertices || !graph->vertices->head || !id) return false;

  curr = graph->vertices->head;
  initial_length = graph->vertices->length;

  for (i = 0; i < initial_length; i++) {
    ds_vertex_t *v;

    if (!curr) break;

    v = (ds_vertex_t *)ds_get_ptr(curr->value);
    if (v && v->id && v->id->data && strcmp(v->id->data, id) == 0) {
      /* Drop the dangling edges other vertices still hold to this one
       * before the vertex goes away; the original left them pointing at
       * recycled memory. */
      ds_list_node_t *scan = graph->vertices->head;
      size_t j, len = graph->vertices->length;

      for (j = 0; j < len && scan; j++) {
        ds_vertex_t *other = (ds_vertex_t *)ds_get_ptr(scan->value);
        if (other && other != v && other->neighbors) {
          size_t k, n = ds_da_len(other->neighbors);
          size_t w = 0;
          for (k = 0; k < n; k++) {
            if (other->neighbors[k] != v) other->neighbors[w++] = other->neighbors[k];
          }
          ds_da_hdr(other->neighbors)->size = w;
        }
        scan = scan->next;
      }

      ds_list_remove(a, graph->vertices, curr);

      /* Release the neighbour array through the same path the finalizer
       * uses. The original recomputed the size by hand from a struct field
       * that does not exist (`old_hdr->size` counts elements, not bytes)
       * and handed the arena a wrong length. */
      ds_vertex_finalize(v, a);
      ds_arena_recycle(a, v, sizeof(ds_vertex_t));
      return true;
    }
    curr = curr->next;
  }
  return false;
}
