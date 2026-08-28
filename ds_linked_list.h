#ifndef ds_linked_list_h
#define ds_linked_list_h

#include <stdbool.h>

#include "common.h"
#include "ds_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ds_list_node_ {
  ds_node_t value;
  struct _ds_list_node_ *next;
  struct _ds_list_node_ *prev;
} ds_list_node_t;

typedef struct {
  DS_ALIGNAS(ARENA_ALIGN) ds_list_node_t *head;
  size_t length;
} ds_list_t;

/* Descriptors are exported so that containers built on top of the list
 * (stack, queue, graph) can trace through it. */
extern const ds_type_descriptor_t ds_list_descriptor;
extern const ds_type_descriptor_t ds_list_node_descriptor;

ds_list_t *ds_list_new(_ds_arena_t_ *a);
void ds_list_append(_ds_arena_t_ *a, ds_list_t *list, ds_node_t value);
void ds_list_prepend(_ds_arena_t_ *a, ds_list_t *list, ds_node_t value);
ds_list_node_t *ds_list_find(const ds_list_t *list, ds_node_t value, bool (*match_func)(ds_node_t, ds_node_t));
bool ds_list_remove(_ds_arena_t_ *a, ds_list_t *list, ds_list_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* ds_linked_list_h */
