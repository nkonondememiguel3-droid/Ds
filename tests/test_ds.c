/*
 * Regression suite for the DS framework.
 *
 * Every test named "regression:" pins down a specific defect that was
 * present before the portability pass; the rest are ordinary API coverage.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_graph.h"
#include "ds_hash_map.h"
#include "ds_linked_list.h"
#include "ds_platform.h"
#include "ds_priority_queue.h"
#include "ds_stack_queue.h"
#include "ds_string.h"
#include "gc.h"

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                   \
  do {                                                                     \
    tests_run++;                                                           \
    if (!(cond)) {                                                         \
      tests_failed++;                                                      \
      printf("  FAIL  %s\n        at %s:%d\n", (msg), __FILE__, __LINE__); \
    }                                                                      \
  } while (0)

#define SECTION(name) printf("\n== %s ==\n", (name))

/* ------------------------------------------------------------------ */
/* Tagged pointers                                                     */
/* ------------------------------------------------------------------ */

static void test_tagging(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  /* Tagging is only defined for ARENA_ALIGN-aligned addresses, which is
   * exactly what the arena promises -- a stack object is not. */
  void *probe = ds_arena_alloc_raw(a, 16);
  ds_node_t n;

  SECTION("tagged pointers");

  n = ds_make_int(42);
  CHECK(ds_get_type(n) == TYPE_INT, "int keeps its tag");
  CHECK(ds_unpack_int(n) == 42, "int round-trips");

  n = ds_make_int(-7);
  CHECK(ds_unpack_int(n) == -7, "regression: negative int round-trips (was UB left-shift)");

  n = ds_make_int(2147483647);
  CHECK(ds_unpack_int(n) == 2147483647, "regression: INT_MAX round-trips");

  n = ds_make_int(-2147483647 - 1);
  CHECK(ds_unpack_int(n) == -2147483647 - 1, "regression: INT_MIN round-trips");

  n = ds_make_float(3.5f);
  CHECK(ds_get_type(n) == TYPE_FLOAT, "float keeps its tag");
  CHECK(ds_unpack_float(n) == 3.5f, "float round-trips");

  n = ds_make_float(-1.0e30f);
  CHECK(ds_unpack_float(n) == -1.0e30f,
        "regression: float with high mantissa bits round-trips (lost top 4 bits on 32-bit)");

  n = ds_make_bool(true);
  CHECK(ds_get_type(n) == TYPE_BOOL && ds_unpack_bool(n), "bool round-trips");

  n = ds_tag_ptr(probe, TYPE_NODE);
  CHECK(ds_get_type(n) == TYPE_NODE, "pointer keeps its tag");
  CHECK(ds_get_ptr(n) == probe, "pointer survives the round trip through the tag");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Arena                                                               */
/* ------------------------------------------------------------------ */

static void test_arena_alignment(void) {
  _ds_arena_t_ *a = ds_arena_new(4096);
  int i;
  int all_aligned = 1;
  void *first;

  SECTION("arena");

  /* This is the invariant the whole tagging scheme rests on: if the arena
   * hands back 8-byte-aligned memory, ds_tag_ptr(p, TYPE_STRING) sets bit 2
   * of a real address and ds_get_ptr can never recover it. The chunk header
   * used to be 40 bytes on LP64, so every payload was misaligned by 8. */
  for (i = 0; i < 500; i++) {
    void *p = ds_arena_alloc_raw(a, (size_t)(i % 97) + 1);
    if (((uintptr_t)p & (ARENA_ALIGN - 1)) != 0) all_aligned = 0;
    if (ds_get_ptr(ds_tag_ptr(p, TYPE_STRING)) != p) all_aligned = 0;
  }
  CHECK(all_aligned, "regression: every allocation is 16-byte aligned and survives tagging");

  first = ds_arena_alloc_raw(a, 64);
  memset(first, 0xAB, 64);
  ds_arena_recycle_raw(a, first, 64);
  CHECK(a->total_free_bytes_in_list >= 64, "recycled bytes land in the free list");

  {
    void *reused = ds_arena_alloc_raw(a, 64);
    CHECK(reused == first, "the free list is consulted before the bump pointer");
    CHECK(((const unsigned char *)reused)[0] == 0, "reused memory is zeroed");
  }

  /* A block smaller than a free-list cell used to be written straight into
   * the neighbouring allocation's storage. */
  {
    void *tiny = ds_arena_alloc_raw(a, 1);
    void *guard = ds_arena_alloc_raw(a, 16);
    memset(guard, 0x5A, 16);
    ds_arena_recycle_raw(a, tiny, 1);
    CHECK(((unsigned char *)guard)[0] == 0x5A,
          "regression: recycling a sub-16-byte block does not scribble on its neighbour");
  }

  ds_arena_destroy(a);
}

static void test_arena_many_chunks(void) {
  _ds_arena_t_ *a = ds_arena_new(1024);
  int i;
  int ok = 1;

  /* Requests larger than the configured chunk size must still work. */
  for (i = 0; i < 20; i++) {
    void *p = ds_arena_alloc_raw(a, 8192);
    if (!p || ((uintptr_t)p & (ARENA_ALIGN - 1)) != 0) ok = 0;
    memset(p, 1, 8192);
  }
  CHECK(ok, "allocations larger than the chunk size get their own chunk");
  CHECK(a->current_chunks >= 20, "oversized allocations grow the chunk list");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

static void test_strings(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_string_t *s, *t, *r;

  SECTION("strings");

  s = ds_str_new(a, "hello world");
  CHECK(s && s->length == 11, "ds_str_new records the length");
  CHECK(strcmp(s->data, "hello world") == 0, "ds_str_new copies the bytes");

  t = ds_str_new(a, "world");
  CHECK(ds_str_find(s, t) == 6, "find locates a substring");

  t = ds_str_new(a, "hello");
  CHECK(ds_str_find(s, t) == 0, "find locates a prefix");

  t = ds_str_new(a, "zebra");
  CHECK(ds_str_find(s, t) == -1, "find reports a miss");

  /* The old scalar tail ran `for (; i <= 32; i++)` with
   * `memcmp(h + i, n + i, n_len)`: on any haystack shorter than 33 bytes it
   * read past the buffer, and it compared the needle against itself. */
  {
    ds_string_t *h = ds_str_new(a, "abcdefghij");
    ds_string_t *n = ds_str_new(a, "hij");
    CHECK(ds_str_find(h, n) == 7, "regression: match at the very end of a short string");
  }
  {
    ds_string_t *h = ds_str_new(a, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab");
    ds_string_t *n = ds_str_new(a, "ab");
    CHECK(ds_str_find(h, n) == 47, "regression: match straddling the AVX2 tail boundary");
  }
  {
    /* Long haystack, needle only in the final vector block. */
    char buf[300];
    ds_string_t *h, *n;
    memset(buf, 'x', sizeof(buf));
    memcpy(buf + 280, "NEEDLE", 6);
    h = ds_str_new_len(a, buf, sizeof(buf));
    n = ds_str_new(a, "NEEDLE");
    CHECK(ds_str_find(h, n) == 280, "regression: match in the last AVX2 block");
  }
  {
    /* Needle whose first byte recurs constantly -- exercises the candidate
     * loop over the comparison mask. Cross-checked against strstr so the
     * expectation cannot drift from the data. */
    char buf[201];
    ds_string_t *h, *n;
    const char *ref;
    memset(buf, 'a', 200);
    buf[200] = '\0';
    buf[199] = 'b';
    h = ds_str_new(a, buf);
    n = ds_str_new(a, "aab");
    ref = strstr(buf, "aab");
    CHECK(ref != NULL && ds_str_find(h, n) == (ptrdiff_t)(ref - buf),
          "many false candidates before the real match (cross-checked with strstr)");
  }

  {
    /* Exhaustive cross-check against strstr over a range of lengths and
     * offsets, so both the AVX2 and the scalar path are covered. */
    char buf[160];
    int len, off;
    int mismatches = 0;
    for (len = 1; len <= 150; len += 7) {
      for (off = 0; off + 3 <= len; off += 5) {
        ds_string_t *h, *n;
        const char *ref;
        ptrdiff_t got;
        memset(buf, 'z', (size_t)len);
        buf[len] = '\0';
        memcpy(buf + off, "qXq", 3);
        h = ds_str_new_len(a, buf, (size_t)len);
        n = ds_str_new(a, "qXq");
        ref = strstr(buf, "qXq");
        got = ds_str_find(h, n);
        if (!ref || got != (ptrdiff_t)(ref - buf)) mismatches++;
      }
    }
    CHECK(mismatches == 0, "regression: find agrees with strstr across every length and offset");
  }

  s = ds_str_new(a, "one");
  t = ds_str_new(a, "two");
  r = ds_str_concat(a, s, t);
  CHECK(r->length == 6 && strcmp(r->data, "onetwo") == 0, "concat");

  s = ds_str_new(a, "  padded \t\n");
  r = ds_str_trim(a, s);
  CHECK(strcmp(r->data, "padded") == 0, "trim");

  s = ds_str_new(a, "abcabcabc");
  r = ds_str_replace(a, s, ds_str_new(a, "abc"), ds_str_new(a, "X"));
  CHECK(strcmp(r->data, "XXX") == 0 && r->length == 3, "replace shrinks");

  r = ds_str_replace(a, s, ds_str_new(a, "b"), ds_str_new(a, "LONG"));
  CHECK(strcmp(r->data, "aLONGcaLONGcaLONGc") == 0, "replace grows");

  r = ds_str_format(a, "%s=%d/%.1f", "k", 7, 2.5);
  CHECK(strcmp(r->data, "k=7/2.5") == 0, "format");

  s = ds_str_new(a, "abc");
  t = ds_str_new(a, "abc");
  CHECK(ds_str_equal(s, t), "equal");
  CHECK(ds_str_compare(s, ds_str_new(a, "abd")) < 0, "compare orders");

  r = ds_str_substr(a, ds_str_new(a, "abcdef"), 2, 3);
  CHECK(strcmp(r->data, "cde") == 0, "substr");

  ds_arena_destroy(a);
}

static void test_split_join(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_string_t **parts;
  ds_string_t *joined;

  SECTION("split / join");

  parts = ds_str_split(a, ds_str_new(a, "a,b,c"), ds_str_new(a, ","));
  CHECK(ds_da_len(parts) == 3, "split produces three fields");
  if (ds_da_len(parts) == 3) {
    CHECK(strcmp(parts[0]->data, "a") == 0, "split field 0");
    CHECK(strcmp(parts[1]->data, "b") == 0, "split field 1");
    CHECK(strcmp(parts[2]->data, "c") == 0, "split field 2");
  }

  joined = ds_str_join(a, parts, ds_str_new(a, "-"));
  CHECK(strcmp(joined->data, "a-b-c") == 0, "join round-trips the split");

  parts = ds_str_split(a, ds_str_new(a, "a,"), ds_str_new(a, ","));
  CHECK(ds_da_len(parts) == 2 && parts[1]->length == 0,
        "regression: a trailing delimiter yields a trailing empty field");

  parts = ds_str_split(a, ds_str_new(a, ",a"), ds_str_new(a, ","));
  CHECK(ds_da_len(parts) == 2 && parts[0]->length == 0, "a leading delimiter yields a leading empty field");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Dynamic arrays                                                      */
/* ------------------------------------------------------------------ */

static void test_dyn_arrays(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_array_t *arr;
  int *xs = NULL;
  int i, ok = 1;

  SECTION("dynamic arrays");

  arr = ds_array_new(a, sizeof(int), 4, 0);
  for (i = 0; i < 1000; i++) ds_array_push(a, arr, &i);
  CHECK(ds_array_len(arr) == 1000, "ds_array_t grows");
  for (i = 0; i < 1000; i++) {
    int *slot = (int *)ds_array_get(arr, (size_t)i);
    if (!slot || *slot != i) ok = 0;
  }
  CHECK(ok, "ds_array_t preserves its contents across reallocation");
  CHECK(ds_array_get(arr, 1000) == NULL, "ds_array_get bounds-checks");
  ds_array_free_raw(a, arr);

  /* The ds_da_* API did not exist at all before this pass. */
  CHECK(ds_da_len(xs) == 0, "a NULL ds_da_ array has length zero");
  for (i = 0; i < 1000; i++) ds_da_push(a, xs, i * 3);
  CHECK(ds_da_len(xs) == 1000, "ds_da_push grows");
  ok = 1;
  for (i = 0; i < 1000; i++)
    if (xs[i] != i * 3) ok = 0;
  CHECK(ok, "ds_da_ preserves its contents across reallocation");
  CHECK((((uintptr_t)xs) & (ARENA_ALIGN - 1)) == 0, "ds_da_ payload stays 16-byte aligned");
  CHECK(ds_da_pop(xs) == 999 * 3, "ds_da_pop returns the last element");
  CHECK(ds_da_len(xs) == 999, "ds_da_pop shrinks");
  ds_da_free(a, xs);
  CHECK(xs == NULL, "ds_da_free clears the handle");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Linked list, stack, queue                                           */
/* ------------------------------------------------------------------ */

static void test_list(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list = ds_list_new(a);
  ds_list_node_t *found;

  SECTION("linked list");

  ds_list_append(a, list, ds_make_int(1));
  ds_list_append(a, list, ds_make_int(2));
  ds_list_append(a, list, ds_make_int(3));
  CHECK(list->length == 3, "append counts");
  CHECK(ds_unpack_int(list->head->value) == 1, "head is the first appended");
  CHECK(ds_unpack_int(list->head->prev->value) == 3, "the list is circular");

  ds_list_prepend(a, list, ds_make_int(0));
  CHECK(list->length == 4 && ds_unpack_int(list->head->value) == 0, "prepend moves the head");

  found = ds_list_find(list, ds_make_int(2), NULL);
  CHECK(found != NULL, "find locates a value");
  CHECK(ds_list_remove(a, list, found), "remove succeeds");
  CHECK(list->length == 3, "remove decrements the length");
  CHECK(ds_list_find(list, ds_make_int(2), NULL) == NULL, "the removed value is gone");

  while (list->length > 1) ds_list_remove(a, list, list->head);
  CHECK(list->length == 1, "drained to one element");
  CHECK(ds_list_remove(a, list, list->head), "the last element can be removed");
  CHECK(list->length == 0 && list->head == NULL, "an emptied list has a NULL head");
  CHECK(!ds_list_remove(a, list, NULL), "removing from an empty list is a no-op");

  ds_arena_destroy(a);
}

static void test_stack_queue(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_stack_t *st = ds_stack_new(a);
  ds_queue_t *q = ds_queue_new(a);

  SECTION("stack / queue");

  CHECK(ds_get_type(ds_stack_pop(a, st)) == TYPE_NIL, "popping an empty stack yields NIL");

  ds_stack_push(a, st, ds_make_int(10));
  ds_stack_push(a, st, ds_make_int(20));
  ds_stack_push(a, st, ds_make_int(30));
  CHECK(ds_stack_size(st) == 3, "stack size");
  CHECK(ds_unpack_int(ds_stack_peek(st)) == 30, "stack peek is LIFO");
  CHECK(ds_unpack_int(ds_stack_pop(a, st)) == 30, "stack pops LIFO");
  CHECK(ds_unpack_int(ds_stack_pop(a, st)) == 20, "stack pops LIFO again");
  CHECK(ds_stack_size(st) == 1, "stack shrinks");
  CHECK(ds_stack_size(NULL) == 0, "stack size is NULL-safe");

  CHECK(ds_get_type(ds_queue_dequeue(a, q)) == TYPE_NIL, "dequeuing an empty queue yields NIL");
  ds_queue_enqueue(a, q, ds_make_int(1));
  ds_queue_enqueue(a, q, ds_make_int(2));
  CHECK(ds_unpack_int(ds_queue_peek(q)) == 1, "queue peek is FIFO");
  CHECK(ds_unpack_int(ds_queue_dequeue(a, q)) == 1, "queue dequeues FIFO");
  CHECK(ds_unpack_int(ds_queue_dequeue(a, q)) == 2, "queue dequeues FIFO again");
  CHECK(ds_queue_size(q) == 0, "queue drains");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Priority queue                                                      */
/* ------------------------------------------------------------------ */

static void test_priority_queue(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_priority_queue_t *pq = ds_pq_new(a);
  static const int input[] = {5, 3, 9, 1, 7, 1, 8, 2, 6, 4};
  int prev = -1;
  int ok = 1;
  size_t i;

  SECTION("priority queue");

  CHECK(ds_get_type(ds_pq_peek(pq)) == TYPE_NIL, "peeking an empty heap yields NIL");

  for (i = 0; i < sizeof(input) / sizeof(input[0]); i++) ds_pq_push(a, pq, ds_make_int(input[i]));
  CHECK(ds_pq_size(pq) == 10, "heap size");
  CHECK(ds_unpack_int(ds_pq_peek(pq)) == 1, "the minimum is at the root");

  /* The old ds_pq_pop wrote `heap[0] = ds_da_pop(heap)` -- one expression
   * with an unsequenced side effect on the length, so the surviving root
   * depended on evaluation order. */
  for (i = 0; i < 10; i++) {
    int v = ds_unpack_int(ds_pq_pop(a, pq));
    if (v < prev) ok = 0;
    prev = v;
  }
  CHECK(ok, "regression: pop yields a non-decreasing sequence");
  CHECK(ds_pq_size(pq) == 0, "the heap drains completely");
  CHECK(ds_get_type(ds_pq_pop(a, pq)) == TYPE_NIL, "popping a drained heap yields NIL");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Hash map                                                            */
/* ------------------------------------------------------------------ */

static void test_hash_map(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_hash_map_t *map = ds_map_new(a, 4);
  char key[32];
  int i, ok = 1;

  SECTION("hash map");

  for (i = 0; i < 500; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    ds_map_put(a, map, ds_str_new(a, key), ds_make_int(i * 2));
  }
  CHECK(map->size == 500, "map holds every key");
  CHECK(map->bucket_count > 4, "map resized itself");

  for (i = 0; i < 500; i++) {
    ds_node_t v;
    snprintf(key, sizeof(key), "k%d", i);
    v = ds_map_get(map, ds_str_new(a, key));
    if (ds_get_type(v) != TYPE_INT || ds_unpack_int(v) != i * 2) ok = 0;
  }
  CHECK(ok, "regression: every key survives the resize chain");

  snprintf(key, sizeof(key), "k7");
  ds_map_put(a, map, ds_str_new(a, key), ds_make_int(-1));
  CHECK(map->size == 500, "overwriting a key does not grow the map");
  CHECK(ds_unpack_int(ds_map_get(map, ds_str_new(a, key))) == -1, "overwrite takes effect");

  CHECK(ds_map_remove(a, map, ds_str_new(a, "k7")), "remove succeeds");
  CHECK(map->size == 499, "remove decrements the size");
  CHECK(ds_get_type(ds_map_get(map, ds_str_new(a, "k7"))) == TYPE_NIL, "the removed key is gone");
  CHECK(!ds_map_remove(a, map, ds_str_new(a, "k7")), "removing twice fails cleanly");
  CHECK(ds_get_type(ds_map_get(map, ds_str_new(a, "absent"))) == TYPE_NIL, "a missing key yields NIL");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Graph                                                               */
/* ------------------------------------------------------------------ */

static void test_graph(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_graph_t *g = ds_graph_new(a);
  ds_vertex_t *va, *vb, *vc;

  SECTION("graph");

  va = ds_graph_add_vertex(a, g, "A", ds_make_int(1));
  vb = ds_graph_add_vertex(a, g, "B", ds_make_int(2));
  vc = ds_graph_add_vertex(a, g, "C", ds_make_int(3));
  CHECK(va && vb && vc, "vertices are created");
  CHECK(g->vertices->length == 3, "the graph holds three vertices");
  CHECK(ds_graph_add_vertex(a, g, "A", ds_make_int(9)) == va, "adding a duplicate id returns the original");

  /* ds_graph_find_vertex used to run the untagged ds_string_t* id through
   * ds_get_ptr(), clearing its low four bits. */
  CHECK(ds_graph_find_vertex(g, "B") == vb, "regression: find_vertex reads an untagged id pointer");
  CHECK(ds_graph_find_vertex(g, "Z") == NULL, "find_vertex reports a miss");

  ds_graph_add_edge(a, va, vb);
  ds_graph_add_edge(a, va, vc);
  ds_graph_add_edge(a, vb, vc);
  ds_graph_add_edge(a, vc, va); /* cycle */
  CHECK(ds_da_len(va->neighbors) == 2, "edges are recorded");

  CHECK(ds_graph_remove_vertex(a, g, "C"), "remove_vertex succeeds");
  CHECK(g->vertices->length == 2, "remove_vertex shrinks the graph");
  CHECK(ds_graph_find_vertex(g, "C") == NULL, "the removed vertex is gone");
  CHECK(ds_da_len(va->neighbors) == 1, "regression: edges into a removed vertex are dropped, not left dangling");

  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Garbage collector                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
  ds_node_t child;
} Cell;

static void cell_mark(ds_node_t node, _ds_arena_t_ *a) {
  Cell *c = (Cell *)ds_get_ptr(node);
  if (c) ds_gc_push_mark_stack_context(a, c->child);
}

static const ds_type_descriptor_t cell_descriptor = {cell_mark, NULL};

static void test_gc(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  Cell *x, *y;
  ds_node_t root;
  size_t live_before, live_after;
  int i;

  SECTION("garbage collector");

  ds_arena_run_gc(NULL); /* must not crash */
  CHECK(1, "regression: running the GC on a NULL arena returns instead of dereferencing it");

  /* A -> B -> A is unreachable but cyclic: refcounting would leak it. */
  x = ARENA_NEW(a, Cell, &cell_descriptor);
  y = ARENA_NEW(a, Cell, &cell_descriptor);
  x->child = ds_tag_ptr(y, TYPE_NODE);
  y->child = ds_tag_ptr(x, TYPE_NODE);

  root = ds_tag_ptr(x, TYPE_NODE);
  ds_gc_register_root(a, &root);

  for (i = 0; i < 200; i++) ARENA_NEW(a, Cell, &cell_descriptor);

  live_before = a->gc_live_allocations;
  CHECK(live_before == 202, "202 managed objects are live");

  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 2, "the rooted cycle survives, the 200 orphans are swept");

  root = ds_make_nil();
  ds_arena_run_gc(a);
  live_after = a->gc_live_allocations;
  CHECK(live_after == 0, "breaking the root collects the cycle itself");

  ds_gc_unregister_root(a, &root);
  ds_arena_destroy(a);
}

static void test_gc_traces_containers(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_list_t *list;
  ds_hash_map_t *map;
  ds_node_t list_root, map_root;
  int i, ok = 1;

  SECTION("gc reachability through containers");

  list = ds_list_new(a);
  list_root = ds_tag_ptr(list, TYPE_NODE);
  ds_gc_register_root(a, &list_root);
  for (i = 0; i < 50; i++) ds_list_append(a, list, ds_make_int(i));

  map = ds_map_new(a, 8);
  map_root = ds_tag_ptr(map, TYPE_NODE);
  ds_gc_register_root(a, &map_root);
  for (i = 0; i < 50; i++) {
    char k[16];
    snprintf(k, sizeof(k), "k%d", i);
    ds_map_put(a, map, ds_str_new(a, k), ds_make_int(i));
  }

  ds_arena_run_gc(a);

  /* Before the type descriptors existed, the list object and the map object
   * survived (they were roots) but every node, entry, key and character
   * buffer they owned was swept out from under them on the first cycle. */
  CHECK(list->length == 50, "the list still reports its length after a collection");
  for (i = 0; i < 50; i++) {
    char k[16];
    ds_node_t v;
    snprintf(k, sizeof(k), "k%d", i);
    v = ds_map_get(map, ds_str_new(a, k));
    if (ds_get_type(v) != TYPE_INT || ds_unpack_int(v) != i) ok = 0;
  }
  CHECK(ok, "regression: map entries and their string keys survive a collection");

  {
    ds_list_node_t *curr = list->head;
    int seen = 0;
    ok = 1;
    do {
      if (ds_unpack_int(curr->value) != seen) ok = 0;
      seen++;
      curr = curr->next;
    } while (curr != list->head && seen < 100);
    CHECK(ok && seen == 50, "regression: list nodes survive a collection and stay in order");
  }

  ds_gc_unregister_root(a, &list_root);
  ds_gc_unregister_root(a, &map_root);
  ds_arena_destroy(a);
}

/* ------------------------------------------------------------------ */

int main(void) {
  printf("DS framework test suite\n");
  printf("  pointer width : %zu bits\n", sizeof(void *) * 8);
  printf("  ds_node_t     : %zu bytes\n", sizeof(ds_node_t));
  printf("  AVX2 at runtime: %s\n", ds_cpu_has_avx2() ? "yes" : "no");

  test_tagging();
  test_arena_alignment();
  test_arena_many_chunks();
  test_strings();
  test_split_join();
  test_dyn_arrays();
  test_list();
  test_stack_queue();
  test_priority_queue();
  test_hash_map();
  test_graph();
  test_gc();
  test_gc_traces_containers();

  printf("\n--------------------------------------------------\n");
  printf("%d checks run, %d failed\n", tests_run, tests_failed);
  printf("--------------------------------------------------\n");
  return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
