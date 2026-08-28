#include "ds_linked_list.h"

#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "gc.h"

/* ------------------------------------------------------------------ */
/* GC integration                                                      */
/* ------------------------------------------------------------------ */

/*
 * Without these descriptors the collector had no way to reach a list's
 * nodes: the list object itself was kept alive by the root, but every
 * ds_list_node_t was swept on the first collection while still linked into
 * the chain, so any traversal afterwards walked recycled memory.
 */

static void ds_list_node_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_list_node_t *n = (ds_list_node_t *)ds_get_ptr(node);
  if (!n) return;
  ds_gc_push_mark_stack_context(a, n->value);
  /* The chain is circular, so following `next` alone already reaches every
   * node. Pushing `prev` as well doubled mark-stack traffic and grew the
   * stack to 32x its necessary size on a 20k-node list, for no extra
   * reachability. */
  if (n->next) ds_gc_push_mark_stack_context(a, ds_tag_ptr(n->next, TYPE_NODE));
}

static void ds_list_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_list_t *list = (ds_list_t *)ds_get_ptr(node);
  if (!list || !list->head) return;
  /* The chain is circular; the mark loop's "already marked" test terminates
   * the walk, so pushing only the head is enough. */
  ds_gc_push_mark_stack_context(a, ds_tag_ptr(list->head, TYPE_NODE));
}

const ds_type_descriptor_t ds_list_descriptor = {ds_list_mark, NULL};
const ds_type_descriptor_t ds_list_node_descriptor = {ds_list_node_mark, NULL};

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

ds_list_t *ds_list_new(_ds_arena_t_ *a) {
  ds_list_t *list;
  if (!a) return NULL;

  list = (ds_list_t *)ds_arena_alloc(a, sizeof(ds_list_t), &ds_list_descriptor);
  list->head = NULL;
  list->length = 0;
  return list;
}

void ds_list_append(_ds_arena_t_ *a, ds_list_t *list, ds_node_t value) {
  ds_list_node_t *new_node;

  if (!a || !list) return;

  new_node = (ds_list_node_t *)ds_arena_alloc(a, sizeof(ds_list_node_t), &ds_list_node_descriptor);
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
  if (!a || !list) return;
  ds_list_append(a, list, value);
  if (list->head) list->head = list->head->prev;
}

ds_list_node_t *ds_list_find(const ds_list_t *list, ds_node_t value, bool (*match_func)(ds_node_t, ds_node_t)) {
  ds_list_node_t *curr;

  if (!list || !list->head) return NULL;

  curr = list->head;
  do {
    if (match_func) {
      if (match_func(curr->value, value)) return curr;
    } else if (curr->value == value) {
      return curr;
    }
    curr = curr->next;
  } while (curr != list->head);

  return NULL;
}

bool ds_list_remove(_ds_arena_t_ *a, ds_list_t *list, ds_list_node_t *node) {
  if (!a || !list || !list->head || !node) return false;
  if (list->length == 0) return false;

  /* The original keyed the "last element" case off `list->length == 1 &&
   * list->head == node`. A node that is its own successor but is not the
   * head (a corrupted or foreign node) fell through to the general path and
   * left list->head pointing at freed memory. Test the topology instead. */
  if (node->next == node) {
    if (list->head != node) return false; /* not a member of this list */
    list->head = NULL;
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    if (list->head == node) list->head = node->next;
  }

  node->next = NULL;
  node->prev = NULL;

  ds_arena_recycle(a, node, sizeof(ds_list_node_t));
  list->length--;
  return true;
}
