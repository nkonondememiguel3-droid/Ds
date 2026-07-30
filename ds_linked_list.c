#include "ds_linked_list.h"

#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"

ds_list_t *ds_list_new(_ds_arena_t_ *a) {
  ds_list_t *list = (ds_list_t *)ds_arena_alloc(a, sizeof(ds_list_t));
  list->head = NULL;
  list->length = 0;
  return list;
}

void ds_list_append(_ds_arena_t_ *a, ds_list_t *list, ds_node_t value) {
  if (!list) return;

  ds_list_node_t *new_node = (ds_list_node_t *)ds_arena_alloc(a, sizeof(ds_list_node_t));
  new_node->value = value;

  if (list->head == NULL) {
    new_node->next = new_node;
    new_node->prev = new_node;
    list->head = new_node;
  } else {
    ds_list_node_t *tail = list->head->prev;

    tail->next = new_node;
    new_node->prev = tail;
    new_node->next = list->head;
    list->head->prev = new_node;
  }
  list->length++;
}

void ds_list_prepend(_ds_arena_t_ *a, ds_list_t *list, ds_node_t value) {
  if (!list) return;

  ds_list_append(a, list, value);
  list->head = list->head->prev;
}

ds_list_node_t *ds_list_find(const ds_list_t *list, ds_node_t value, bool (*match_func)(ds_node_t, ds_node_t)) {
  if (!list || !list->head) return NULL;

  ds_list_node_t *curr = list->head;
  do {
    if (match_func) {
      if (match_func(curr->value, value)) return curr;
    } else {
      if (curr->value == value) return curr;
    }
    curr = curr->next;
  } while (curr != list->head);

  return NULL;
}

bool ds_list_remove(_ds_arena_t_ *a, ds_list_t *list, ds_list_node_t *node) {
  if (!list || !list->head || !node) return false;

  if (list->length == 1 && list->head == node) {
    list->head = NULL;
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;

    if (list->head == node) {
      list->head = node->next;
    }
  }

  ds_arena_recycle(a, node);
  list->length--;
  return true;
}
