#ifndef GC_H
#define GC_H

#include "common.h"

typedef void (*ds_gc_mark_extension_func)(ds_node_t node);

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr);
void ds_gc_register_allocation(void *ptr);
void ds_gc_unregister_allocation(void *ptr);
void ds_arena_run_gc(_ds_arena_t_ *a);
void ds_gc_destroy(void);

void ds_gc_set_mark_extension(ds_gc_mark_extension_func func);

#endif
