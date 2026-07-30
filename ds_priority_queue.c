#include "ds_priority_queue.h"

#include <stdlib.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"

ds_priority_queue_t *ds_pq_new(_ds_arena_t_ *a) {
  ds_priority_queue_t *pq = (ds_priority_queue_t *)ds_arena_alloc(a, sizeof(ds_priority_queue_t));
  pq->heap_array = NULL;
  return pq;
}

size_t ds_pq_size(const ds_priority_queue_t *pq) { return pq ? ds_da_len(pq->heap_array) : 0; }

ds_node_t ds_pq_peek(const ds_priority_queue_t *pq) {
  if (!pq || ds_pq_size(pq) == 0) {
    return ds_tag_ptr(NULL, TYPE_NIL);
  }
  return pq->heap_array[0];
}

static void sift_up(ds_node_t *arr, size_t index) {
  while (index > 0) {
    size_t parent_idx = (index - 1) / 2;

    if (ds_unpack_int(arr[index]) >= ds_unpack_int(arr[parent_idx])) {
      break;
    }

    ds_node_t tmp = arr[index];
    arr[index] = arr[parent_idx];
    arr[parent_idx] = tmp;

    index = parent_idx;
  }
}

static void sift_down(ds_node_t *arr, size_t size, size_t index) {
  size_t left_child;
  while ((left_child = 2 * index + 1) < size) {
    size_t right_child = left_child + 1;
    size_t smallest = left_child;

    if (right_child < size && ds_unpack_int(arr[right_child]) < ds_unpack_int(arr[left_child])) {
      smallest = right_child;
    }

    if (ds_unpack_int(arr[index]) <= ds_unpack_int(arr[smallest])) {
      break;
    }

    ds_node_t tmp = arr[index];
    arr[index] = arr[smallest];
    arr[smallest] = tmp;

    index = smallest;
  }
}

void ds_pq_push(_ds_arena_t_ *a, ds_priority_queue_t *pq, ds_node_t value) {
  if (!pq) return;

  ds_da_push(a, pq->heap_array, value);

  sift_up(pq->heap_array, ds_da_len(pq->heap_array) - 1);
}

ds_node_t ds_pq_pop(_ds_arena_t_ *a, ds_priority_queue_t *pq) {
  if (!pq || ds_pq_size(pq) == 0) {
    return ds_tag_ptr(NULL, TYPE_NIL);
  }

  ds_node_t min_value = pq->heap_array[0];
  size_t current_len = ds_da_len(pq->heap_array);

  if (current_len == 1) {
    ds_da_pop(pq->heap_array);
    return min_value;
  }

  pq->heap_array[0] = ds_da_pop(pq->heap_array);

  sift_down(pq->heap_array, ds_da_len(pq->heap_array), 0);

  return min_value;
}
