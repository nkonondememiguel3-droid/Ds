/*
 * Container fuzzer: hash map, linked list and priority queue, each driven
 * through randomised operation sequences and compared against a reference
 * model implemented with plain arrays and linear search.
 *
 * The model is deliberately stupid -- O(n) everything, no cleverness -- so
 * that when the two disagree it is the library that is wrong.
 *
 * Collections are interleaved with the operations. Everything under test is
 * rooted, so a collection must be invisible: any divergence after one means
 * the collector reclaimed something still reachable.
 */

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_fuzz.h"
#include "ds_hash_map.h"
#include "ds_linked_list.h"
#include "ds_priority_queue.h"
#include "ds_string.h"
#include "gc.h"

const char *ds_fuzz_name = "fuzz_containers";

#define MAX_KEYS 128
#define KEY_LEN 12

/* ------------------------------------------------------------------ */
/* Hash map against a linear-scan model                                */
/* ------------------------------------------------------------------ */

typedef struct {
  char key[KEY_LEN + 1];
  size_t key_len;
  int value;
  int present;
} map_entry_t;

static map_entry_t model[MAX_KEYS];
static size_t model_count;

/*
 * Any slot carrying these key bytes, present or not.
 *
 * Searching only `present` slots was a bug in this model: putting a key,
 * removing it, then putting it again appended a second slot with the same
 * key, and the stale slot then asserted the key was absent while the live
 * one asserted it was present. The map was right both times.
 */
static map_entry_t *model_slot(const char *k, size_t kl) {
  size_t i;
  for (i = 0; i < model_count; i++) {
    if (model[i].key_len == kl && memcmp(model[i].key, k, kl) == 0) return &model[i];
  }
  return NULL;
}

/* Only slots that are currently in the map. */
static map_entry_t *model_find(const char *k, size_t kl) {
  map_entry_t *e = model_slot(k, kl);
  return (e && e->present) ? e : NULL;
}

static size_t model_size(void) {
  size_t i, n = 0;
  for (i = 0; i < model_count; i++)
    if (model[i].present) n++;
  return n;
}

static void map_verify(_ds_arena_t_ *a, ds_hash_map_t *m) {
  size_t i;

  FZ_CHECK(m->size == model_size(), "map size diverged from the model");

  for (i = 0; i < model_count; i++) {
    ds_string_t *k = ds_str_new_len(a, model[i].key, model[i].key_len);
    ds_node_t v = ds_map_get(m, k);
    if (model[i].present) {
      FZ_CHECK(ds_get_type(v) == TYPE_INT, "a key present in the model was missing from the map");
      FZ_CHECK(ds_unpack_int(v) == model[i].value, "the map returned the wrong value for a key");
    } else {
      FZ_CHECK(ds_get_type(v) == TYPE_NIL, "a key absent from the model was found in the map");
    }
  }
}

static void fuzz_map(_ds_arena_t_ *a, ds_fuzz_t *f) {
  ds_hash_map_t *m = ds_map_new(a, (size_t)1u << fz_range(f, 0, 4));
  ds_node_t root = ds_tag_ptr(m, TYPE_NODE);
  int ops;

  ds_gc_register_root(a, &root);
  model_count = 0;
  memset(model, 0, sizeof(model));

  for (ops = 0; ops < 300 && !fz_empty(f); ops++) {
    char key[KEY_LEN + 1];
    size_t kl = fz_bytes_narrow(f, key, KEY_LEN);
    ds_string_t *k = ds_str_new_len(a, key, kl);

    switch (fz_u8(f) % 8) {
      case 0:
      case 1:
      case 2:
      case 3: { /* put */
        int value = (int)fz_u32(f);
        map_entry_t *e = model_slot(key, kl); /* reuse the slot if we have seen this key */
        ds_map_put(a, m, k, ds_make_int(value));
        if (e) {
          e->value = value;
          e->present = 1;
        } else if (model_count < MAX_KEYS) {
          memcpy(model[model_count].key, key, kl);
          model[model_count].key_len = kl;
          model[model_count].value = value;
          model[model_count].present = 1;
          model_count++;
        } else {
          return; /* model is full; stop rather than lose track */
        }
        break;
      }
      case 4:
      case 5: { /* get */
        map_entry_t *e = model_find(key, kl);
        ds_node_t v = ds_map_get(m, k);
        if (e) {
          FZ_CHECK(ds_get_type(v) == TYPE_INT && ds_unpack_int(v) == e->value, "ds_map_get returned the wrong value");
        } else {
          FZ_CHECK(ds_get_type(v) == TYPE_NIL, "ds_map_get found a key that was never inserted");
        }
        break;
      }
      case 6: { /* remove */
        map_entry_t *e = model_find(key, kl);
        bool removed = ds_map_remove(a, m, k);
        FZ_CHECK(removed == (e != NULL), "ds_map_remove disagreed with the model about presence");
        if (e) e->present = 0;
        break;
      }
      default: /* collect: the map is rooted, so nothing may change */
        ds_arena_run_gc(a);
        break;
    }
    map_verify(a, m);
  }

  ds_gc_unregister_root(a, &root);
}

/* ------------------------------------------------------------------ */
/* Linked list against an array model                                  */
/* ------------------------------------------------------------------ */

static void fuzz_list(_ds_arena_t_ *a, ds_fuzz_t *f) {
  static int ref[MAX_KEYS];
  size_t ref_count = 0;
  ds_list_t *l = ds_list_new(a);
  ds_node_t root = ds_tag_ptr(l, TYPE_NODE);
  int ops;

  ds_gc_register_root(a, &root);

  for (ops = 0; ops < 300 && !fz_empty(f); ops++) {
    switch (fz_u8(f) % 8) {
      case 0:
      case 1:
      case 2: /* append */
        if (ref_count < MAX_KEYS) {
          int v = (int)(fz_u32(f) & 0xFFFF);
          ds_list_append(a, l, ds_make_int(v));
          ref[ref_count++] = v;
        }
        break;
      case 3:
      case 4: /* prepend */
        if (ref_count < MAX_KEYS) {
          int v = (int)(fz_u32(f) & 0xFFFF);
          size_t i;
          ds_list_prepend(a, l, ds_make_int(v));
          for (i = ref_count; i > 0; i--) ref[i] = ref[i - 1];
          ref[0] = v;
          ref_count++;
        }
        break;
      case 5:
      case 6: /* remove the head */
        if (ref_count > 0) {
          size_t i;
          FZ_CHECK(ds_list_remove(a, l, l->head), "removing the head of a non-empty list failed");
          for (i = 0; i + 1 < ref_count; i++) ref[i] = ref[i + 1];
          ref_count--;
        }
        break;
      default:
        ds_arena_run_gc(a);
        break;
    }

    /* Walk the ring and compare it element by element. */
    FZ_CHECK(l->length == ref_count, "list length diverged from the model");
    if (ref_count == 0) {
      FZ_CHECK(l->head == NULL, "an empty list kept a non-NULL head");
    } else {
      ds_list_node_t *curr = l->head;
      size_t i;
      for (i = 0; i < ref_count; i++) {
        FZ_CHECK(curr != NULL, "the ring ended early");
        FZ_CHECK(ds_unpack_int(curr->value) == ref[i], "list contents diverged from the model");
        curr = curr->next;
      }
      FZ_CHECK(curr == l->head, "the ring did not close back on the head");
    }
  }

  ds_gc_unregister_root(a, &root);
}

/* ------------------------------------------------------------------ */
/* Priority queue: heap property plus a sorted drain                   */
/* ------------------------------------------------------------------ */

static void fuzz_pq(_ds_arena_t_ *a, ds_fuzz_t *f) {
  ds_priority_queue_t *pq = ds_pq_new(a);
  ds_node_t root = ds_tag_ptr(pq, TYPE_NODE);
  int pushed = 0, ops;
  int min_seen = 0x7FFFFFFF;

  ds_gc_register_root(a, &root);

  for (ops = 0; ops < 300 && !fz_empty(f); ops++) {
    if ((fz_u8(f) % 4) == 3 && pushed > 0) {
      ds_arena_run_gc(a);
      FZ_CHECK((int)ds_pq_size(pq) == pushed, "a rooted heap lost elements to a collection");
    } else {
      int v = (int)(fz_u32(f) & 0xFFFF);
      ds_pq_push(a, pq, ds_make_int(v));
      pushed++;
      if (v < min_seen) min_seen = v;
      FZ_CHECK((int)ds_pq_size(pq) == pushed, "heap size did not follow the pushes");
      FZ_CHECK(ds_unpack_int(ds_pq_peek(pq)) == min_seen, "the heap root is not the smallest value pushed");
    }
  }

  /* Draining must produce a non-decreasing sequence, and exactly as many
   * elements as went in. */
  {
    int prev = -1, drained = 0;
    while (ds_pq_size(pq) > 0) {
      int v = ds_unpack_int(ds_pq_pop(a, pq));
      FZ_CHECK(v >= prev, "ds_pq_pop produced a decreasing sequence");
      prev = v;
      drained++;
    }
    FZ_CHECK(drained == pushed, "the heap drained a different number of elements than were pushed");
    FZ_CHECK(ds_get_type(ds_pq_pop(a, pq)) == TYPE_NIL, "popping a drained heap did not yield NIL");
  }

  ds_gc_unregister_root(a, &root);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  ds_fuzz_t f;
  _ds_arena_t_ *arena;

  if (size < 8) return 0;

  fz_init(&f, data, size);
  arena = ds_arena_new(0);

  switch (fz_u8(&f) % 3) {
    case 0:
      fuzz_map(arena, &f);
      break;
    case 1:
      fuzz_list(arena, &f);
      break;
    default:
      fuzz_pq(arena, &f);
      break;
  }

  ds_arena_run_gc(arena);
  ds_arena_destroy(arena);
  return 0;
}
