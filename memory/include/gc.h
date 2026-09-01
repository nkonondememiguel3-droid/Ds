#ifndef GC_H
#define GC_H

/*
 * Descriptor-driven mark and sweep, layered on the arena.
 *
 * This is a separate library (libds_gc.a) built on top of libds_arena.a.
 * The dependency runs one way only: the collector knows about arena blocks,
 * the arena knows nothing about collection. A program that only wants a fast
 * allocator includes ds_arena.h, links libds_arena.a, and none of this is
 * compiled into it.
 *
 * The collector holds no state of its own inside the arena struct. The first
 * call that needs it attaches a ds_gc_t (allocated from the arena as a raw,
 * untraced block) to the arena's `collector` slot, along with the teardown
 * hook ds_arena_destroy calls.
 */

#include "ds_arena.h"
#include "ds_value.h"

/*
 * The bridge between the two layers: tags live in the low bits of a pointer,
 * so every arena block must be aligned well enough to leave those bits free.
 * The requirement belongs to the collector -- the arena has no tags -- so it
 * is asserted here rather than in either of the headers above.
 */
DS_STATIC_ASSERT(ARENA_ALIGN >= (1 << DS_TAG_BITS), "ARENA_ALIGN must leave the low tag bits free for ds_node_t");

/*
 * The per-type policy. `mark` enqueues everything the object references;
 * `finalize` releases the raw buffers it owns. Either may be NULL: a leaf
 * object with no references needs no mark, an object owning no raw memory
 * needs no finalize.
 *
 * This is the type ds_arena.h forward-declares and stores, unexamined, in
 * each managed block's header.
 */
struct ds_type_descriptor {
  void (*mark)(ds_node_t node, _ds_arena_t_ *a);
  void (*finalize)(void *ptr, _ds_arena_t_ *a);
};

typedef struct _ds_gc_root_ {
  ds_node_t *variable_pointer;
  struct _ds_gc_root_ *next;
} _ds_gc_root_t_;

/* Everything the collector needs, and nothing the arena has to carry. */
typedef struct ds_gc_state {
  _ds_gc_root_t_ *roots;
  ds_node_t *mark_stack;
  size_t mark_stack_top;
  size_t mark_stack_cap;
} ds_gc_t;

#ifdef __cplusplus
extern "C" {
#endif

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr);
void ds_gc_unregister_root(_ds_arena_t_ *a, ds_node_t *var_ptr);

void ds_gc_register_allocation(_ds_arena_t_ *a, void *ptr, size_t size, const ds_type_descriptor_t *desc);
void ds_gc_unregister_allocation(_ds_arena_t_ *a, void *ptr);

/* Called from a type descriptor's `mark` callback to enqueue a referenced
 * node. Nodes that are not TYPE_NODE or TYPE_STRING are ignored. */
void ds_gc_push_mark_stack_context(_ds_arena_t_ *a, ds_node_t node);

void ds_gc_run(_ds_arena_t_ *a);

/* The original spelling of ds_gc_run, kept because it is what every existing
 * caller and the README use. It lives in the collector, not the arena. */
void ds_arena_run_gc(_ds_arena_t_ *a);

/* The collector state attached to this arena, or NULL if nothing has needed
 * one yet. */
ds_gc_t *ds_gc_state_of(const _ds_arena_t_ *a);

#ifdef __cplusplus
}
#endif

#endif
