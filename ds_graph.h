#ifndef ds_graph_h
#define ds_graph_h

#include "common.h"
#include "ds_arena.h"
#include "ds_linked_list.h"
#include "ds_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ds_vertex_ {
  ds_string_t *id;
  ds_node_t value;
  struct _ds_vertex_ **neighbors; /* ds_da_* dynamic array */
} ds_vertex_t;

typedef struct {
  DS_ALIGNAS(ARENA_ALIGN) ds_list_t *vertices;
} ds_graph_t;

extern const ds_type_descriptor_t ds_graph_descriptor;
extern const ds_type_descriptor_t ds_vertex_descriptor;

ds_graph_t *ds_graph_new(_ds_arena_t_ *a);
ds_vertex_t *ds_graph_add_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id, ds_node_t value);
void ds_graph_add_edge(_ds_arena_t_ *a, ds_vertex_t *from, ds_vertex_t *to);
ds_vertex_t *ds_graph_find_vertex(const ds_graph_t *graph, const char *id);
bool ds_graph_remove_vertex(_ds_arena_t_ *a, ds_graph_t *graph, const char *id);

#ifdef __cplusplus
}
#endif

#endif /* ds_graph_h */
