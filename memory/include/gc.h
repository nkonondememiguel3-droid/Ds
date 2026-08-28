#ifndef GC_H
#define GC_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr);
void ds_gc_unregister_root(_ds_arena_t_ *a, ds_node_t *var_ptr);

void ds_gc_register_allocation(_ds_arena_t_ *a, void *ptr, size_t size, const ds_type_descriptor_t *desc);
void ds_gc_unregister_allocation(_ds_arena_t_ *a, void *ptr);

void ds_arena_run_gc(_ds_arena_t_ *a);

/* Called from a type descriptor's `mark` callback to enqueue a referenced
 * node. Nodes that are not TYPE_NODE or TYPE_STRING are ignored. */
void ds_gc_push_mark_stack_context(_ds_arena_t_ *a, ds_node_t node);

#ifdef __cplusplus
}
#endif

#endif
