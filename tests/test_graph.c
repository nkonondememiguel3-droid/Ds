/* Adjacency-list graph. */

#include <stdio.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_graph.h"
#include "ds_test.h"
#include "gc.h"

static void test_vertices(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_graph_t *g = ds_graph_new(a);
  ds_vertex_t *va, *vb;

  SECTION("vertices");

  CHECK(g != NULL && g->vertices != NULL && g->vertices->length == 0, "a new graph is empty");
  CHECK(ds_graph_find_vertex(g, "anything") == NULL, "an empty graph finds nothing");

  va = ds_graph_add_vertex(a, g, "A", ds_make_int(1));
  vb = ds_graph_add_vertex(a, g, "B", ds_make_int(2));
  CHECK(va != NULL && vb != NULL, "vertices are created");
  CHECK(g->vertices->length == 2, "the graph holds both");
  CHECK(ds_unpack_int(va->value) == 1, "the payload is stored");

  CHECK(ds_graph_add_vertex(a, g, "A", ds_make_int(9)) == va, "a duplicate id returns the original");
  CHECK(g->vertices->length == 2, "and does not add a second vertex");

  /* find_vertex used to run the untagged ds_string_t* id through
   * ds_get_ptr(), clearing four real address bits. */
  CHECK(ds_graph_find_vertex(g, "B") == vb, "regression: find_vertex reads an untagged id pointer");
  CHECK(ds_graph_find_vertex(g, "A") == va, "and finds the first vertex too");
  CHECK(ds_graph_find_vertex(g, "Z") == NULL, "find_vertex reports a miss");
  CHECK(ds_graph_find_vertex(NULL, "A") == NULL, "find_vertex is NULL-safe");
  CHECK(ds_graph_add_vertex(a, g, NULL, ds_make_int(0)) == NULL, "a NULL id is rejected");

  ds_arena_destroy(a);
}

static void test_edges(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_graph_t *g = ds_graph_new(a);
  ds_vertex_t *va = ds_graph_add_vertex(a, g, "A", ds_make_int(1));
  ds_vertex_t *vb = ds_graph_add_vertex(a, g, "B", ds_make_int(2));
  ds_vertex_t *vc = ds_graph_add_vertex(a, g, "C", ds_make_int(3));

  SECTION("edges");

  CHECK(ds_da_len(va->neighbors) == 0, "a fresh vertex has no neighbours");

  ds_graph_add_edge(a, va, vb);
  ds_graph_add_edge(a, va, vc);
  CHECK(ds_da_len(va->neighbors) == 2, "edges are recorded");
  CHECK(va->neighbors[0] == vb && va->neighbors[1] == vc, "in insertion order");

  ds_graph_add_edge(a, vb, vc);
  ds_graph_add_edge(a, vc, va); /* closes a cycle */
  CHECK(ds_da_len(vc->neighbors) == 1 && vc->neighbors[0] == va, "a cycle is representable");

  ds_graph_add_edge(a, NULL, vb);
  ds_graph_add_edge(a, va, NULL);
  CHECK(ds_da_len(va->neighbors) == 2, "add_edge is NULL-safe");

  ds_graph_add_edge(a, va, va);
  CHECK(ds_da_len(va->neighbors) == 3, "self edges are allowed");

  ds_arena_destroy(a);
}

static void test_remove_vertex(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_graph_t *g = ds_graph_new(a);
  ds_vertex_t *va = ds_graph_add_vertex(a, g, "A", ds_make_int(1));
  ds_vertex_t *vb = ds_graph_add_vertex(a, g, "B", ds_make_int(2));
  ds_vertex_t *vc = ds_graph_add_vertex(a, g, "C", ds_make_int(3));

  SECTION("remove_vertex");

  ds_graph_add_edge(a, va, vb);
  ds_graph_add_edge(a, va, vc);
  ds_graph_add_edge(a, vb, vc);
  ds_graph_add_edge(a, vc, va);

  CHECK(ds_graph_remove_vertex(a, g, "C"), "remove_vertex succeeds");
  CHECK(g->vertices->length == 2, "the graph shrinks");
  CHECK(ds_graph_find_vertex(g, "C") == NULL, "the removed vertex is gone");

  /* The old implementation recycled the vertex without scrubbing it from
   * anyone else's neighbour array, leaving pointers into recycled memory. */
  CHECK(ds_da_len(va->neighbors) == 1 && va->neighbors[0] == vb,
        "regression: edges into a removed vertex are dropped, not left dangling");
  CHECK(ds_da_len(vb->neighbors) == 0, "and dropped from every other vertex too");

  CHECK(!ds_graph_remove_vertex(a, g, "C"), "removing twice fails cleanly");
  CHECK(!ds_graph_remove_vertex(a, g, "nope"), "removing an absent id fails cleanly");
  CHECK(!ds_graph_remove_vertex(a, NULL, "A"), "remove_vertex is NULL-safe");

  CHECK(ds_graph_remove_vertex(a, g, "A"), "the head vertex can be removed");
  CHECK(ds_graph_remove_vertex(a, g, "B"), "the last vertex can be removed");
  CHECK(g->vertices->length == 0, "the graph empties");

  ds_arena_destroy(a);
}

static void test_larger_graph(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_graph_t *g = ds_graph_new(a);
  char id[16];
  int i, ok = 1;

  SECTION("a larger graph");

  for (i = 0; i < 200; i++) {
    snprintf(id, sizeof(id), "v%d", i);
    ds_graph_add_vertex(a, g, id, ds_make_int(i));
  }
  CHECK(g->vertices->length == 200, "200 vertices are stored");

  /* Ring plus chords. */
  for (i = 0; i < 200; i++) {
    char other[16];
    ds_vertex_t *from, *to;
    snprintf(id, sizeof(id), "v%d", i);
    snprintf(other, sizeof(other), "v%d", (i + 1) % 200);
    from = ds_graph_find_vertex(g, id);
    to = ds_graph_find_vertex(g, other);
    if (!from || !to) ok = 0;
    ds_graph_add_edge(a, from, to);
  }
  CHECK(ok, "every vertex is findable by id");

  for (i = 0; i < 200; i++) {
    snprintf(id, sizeof(id), "v%d", i);
    if (ds_da_len(ds_graph_find_vertex(g, id)->neighbors) != 1) ok = 0;
  }
  CHECK(ok, "each vertex has its single ring edge");

  ds_arena_destroy(a);
}

static void test_gc_interaction(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_graph_t *g = ds_graph_new(a);
  ds_node_t root;
  ds_vertex_t *va, *vb;

  SECTION("collection");

  va = ds_graph_add_vertex(a, g, "A", ds_make_int(1));
  vb = ds_graph_add_vertex(a, g, "B", ds_make_int(2));
  ds_graph_add_edge(a, va, vb);
  ds_graph_add_edge(a, vb, va); /* mutual reference: a refcount would leak */

  root = ds_tag_ptr(g, TYPE_NODE);
  ds_gc_register_root(a, &root);

  ds_arena_run_gc(a);
  CHECK(g->vertices->length == 2, "a rooted graph keeps its vertices");
  CHECK(ds_graph_find_vertex(g, "A") == va, "vertex ids survive the collection");
  CHECK(ds_da_len(va->neighbors) == 1 && va->neighbors[0] == vb, "neighbour arrays survive too");

  ds_gc_unregister_root(a, &root);
  ds_arena_run_gc(a);
  CHECK(a->gc_live_allocations == 0, "an unrooted graph is collected despite the reference cycle");

  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("graph");
  test_vertices();
  test_edges();
  test_remove_vertex();
  test_larger_graph();
  test_gc_interaction();
  return ds_test_end();
}
