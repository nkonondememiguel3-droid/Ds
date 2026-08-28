/*
 * Graph: vertex insertion, lookup, edge insertion, removal, traversal.
 *
 * Vertex lookup is a linear scan of the vertex list with a strcmp per
 * candidate, so the sizes here are deliberately modest -- the quadratic
 * shape of build-by-id is itself the interesting result.
 */

#include <stdio.h>

#include "ds_arena.h"
#include "ds_bench.h"
#include "ds_dyn_array.h"
#include "ds_graph.h"
#include "ds_stack_queue.h"
#include "gc.h"

#define V 2000

static void bench_add_vertex(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_graph_t *g = ds_graph_new(a);
    char id[16];
    int i;
    ds_bench_start();
    for (i = 0; i < V; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      ds_graph_add_vertex(a, g, id, ds_make_int(i));
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  /* add_vertex calls find_vertex first to reject duplicates, so building a
   * graph this way is O(V^2) in total. */
  ds_bench_report("add_vertex (duplicate check scans)", V);
}

static void bench_find_vertex(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_graph_t *g = ds_graph_new(a);
    char id[16];
    int i;
    for (i = 0; i < V; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      ds_graph_add_vertex(a, g, id, ds_make_int(i));
    }
    ds_bench_start();
    for (i = 0; i < V; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      ds_bench_keep(ds_graph_find_vertex(g, id) ? 1u : 0u);
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("find_vertex, linear scan by id", V);
}

static void bench_add_edge(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_graph_t *g = ds_graph_new(a);
    ds_vertex_t **vs = (ds_vertex_t **)ds_arena_alloc_raw(a, V * sizeof(ds_vertex_t *));
    char id[16];
    int i, j;
    for (i = 0; i < V; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      vs[i] = ds_graph_add_vertex(a, g, id, ds_make_int(i));
    }
    /* 10 out-edges per vertex, with the handles already in hand. */
    ds_bench_start();
    for (i = 0; i < V; i++)
      for (j = 1; j <= 10; j++) ds_graph_add_edge(a, vs[i], vs[(i + j) % V]);
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("add_edge (handles already resolved)", V * 10);
}

static void bench_traverse_bfs(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_graph_t *g = ds_graph_new(a);
    ds_vertex_t **vs = (ds_vertex_t **)ds_arena_alloc_raw(a, V * sizeof(ds_vertex_t *));
    ds_queue_t *q;
    char id[16];
    int i, j, visited = 0;

    for (i = 0; i < V; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      vs[i] = ds_graph_add_vertex(a, g, id, ds_make_int(i));
    }
    for (i = 0; i < V; i++)
      for (j = 1; j <= 4; j++) ds_graph_add_edge(a, vs[i], vs[(i + j) % V]);

    q = ds_queue_new(a);

    /* Breadth-first over the ring, bounded by an explicit visit budget
     * rather than a visited set -- this measures queue and adjacency
     * traffic, not set membership. */
    ds_bench_start();
    ds_queue_enqueue(a, q, ds_tag_ptr(vs[0], TYPE_NODE));
    while (ds_queue_size(q) > 0 && visited < V * 4) {
      ds_vertex_t *v = (ds_vertex_t *)ds_get_ptr(ds_queue_dequeue(a, q));
      size_t k, n;
      if (!v) continue;
      visited++;
      n = ds_da_len(v->neighbors);
      for (k = 0; k < n && ds_queue_size(q) < V; k++) ds_queue_enqueue(a, q, ds_tag_ptr(v->neighbors[k], TYPE_NODE));
    }
    ds_bench_stop();
    ds_bench_keep((size_t)visited);
    ds_arena_destroy(a);
  }
  ds_bench_report("BFS walk over an 8k-edge graph", V * 4);
}

static void bench_remove_vertex(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_graph_t *g = ds_graph_new(a);
    ds_vertex_t **vs = (ds_vertex_t **)ds_arena_alloc_raw(a, 400 * sizeof(ds_vertex_t *));
    char id[16];
    int i, j;
    for (i = 0; i < 400; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      vs[i] = ds_graph_add_vertex(a, g, id, ds_make_int(i));
    }
    for (i = 0; i < 400; i++)
      for (j = 1; j <= 4; j++) ds_graph_add_edge(a, vs[i], vs[(i + j) % 400]);

    /* Each removal also scrubs the departing vertex out of every other
     * vertex's neighbour array, so this is quadratic by construction. */
    ds_bench_start();
    for (i = 0; i < 400; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      ds_bench_keep(ds_graph_remove_vertex(a, g, id) ? 1u : 0u);
    }
    ds_bench_stop();
    ds_arena_destroy(a);
  }
  ds_bench_report("remove_vertex (scrubs incoming edges)", 400);
}

static void bench_collect(void) {
  int rep;
  DS_BENCH_LOOP(rep) {
    _ds_arena_t_ *a = ds_arena_new(16u * 1024u * 1024u);
    ds_graph_t *g = ds_graph_new(a);
    ds_vertex_t **vs = (ds_vertex_t **)ds_arena_alloc_raw(a, V * sizeof(ds_vertex_t *));
    ds_node_t root;
    char id[16];
    int i, j;
    for (i = 0; i < V; i++) {
      snprintf(id, sizeof(id), "v%d", i);
      vs[i] = ds_graph_add_vertex(a, g, id, ds_make_int(i));
    }
    for (i = 0; i < V; i++)
      for (j = 1; j <= 4; j++) ds_graph_add_edge(a, vs[i], vs[(i + j) % V]);

    root = ds_tag_ptr(g, TYPE_NODE);
    ds_gc_register_root(a, &root);
    ds_bench_start();
    ds_arena_run_gc(a);
    ds_bench_stop();
    ds_gc_unregister_root(a, &root);
    ds_arena_destroy(a);
  }
  ds_bench_report("trace a rooted cyclic graph", V);
}

int main(void) {
  ds_bench_banner("graph");
  bench_add_vertex();
  bench_find_vertex();
  bench_add_edge();
  bench_traverse_bfs();
  bench_remove_vertex();
  bench_collect();
  ds_bench_footer();
  return 0;
}
