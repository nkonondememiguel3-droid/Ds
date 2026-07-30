#ifndef ds_linked_list_h
#define ds_linked_list_h

#include <stdbool.h>

#include "common.h"

typedef struct _ds_list_node_ {
  ds_node_t value;
  struct _ds_list_node_ *next;
  struct _ds_list_node_ *prev;
} ds_list_node_t;

typedef struct __attribute__((aligned(ARENA_ALIGN))) {
  ds_list_node_t *head;
  size_t length;
} ds_list_t;

extern ds_list_t *ds_list_new(_ds_arena_t_ *a);
extern void ds_list_append(_ds_arena_t_ *a, ds_list_t *list, ds_node_t value);
extern void ds_list_prepend(_ds_arena_t_ *a, ds_list_t *list, ds_node_t value);
extern ds_list_node_t *ds_list_find(const ds_list_t *list, ds_node_t value, bool (*match_func)(ds_node_t, ds_node_t));
extern bool ds_list_remove(_ds_arena_t_ *a, ds_list_t *list, ds_list_node_t *node);

#endif  // ds_linked_list_h
