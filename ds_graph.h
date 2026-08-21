#ifndef ds_graph_h
#define ds_graph_h

#include <stdalign.h>
#include "common.h"
#include "ds_linked_list.h"
#include "ds_string.h"

struct _ds_vertex_;

typedef struct _ds_vertex_ {
  ds_string_t *id;
  ds_node_t value;
  struct _ds_vertex_ **neighbors;
} ds_vertex_t;

typedef struct {
  alignas(16) ds_list_t *vertices;
} ds_graph_t;

extern ds_graph_t *ds_graph_new(_ds_arena_t_ *a);
extern ds_vertex_t *ds_graph_add_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id, ds_node_t value);
extern void ds_graph_add_edge(_ds_arena_t_ *a, ds_vertex_t *from, ds_vertex_t *to);
extern ds_vertex_t *ds_graph_find_vertex(const ds_graph_t *graph, const char *id);
extern bool ds_graph_remove_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id);

#endif  // ds_graph_h
