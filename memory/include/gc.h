#ifndef GC_H
#define GC_H

#include "common.h"

extern void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr);

extern void ds_gc_register_allocation(void *ptr);

extern void ds_arena_run_gc(_ds_arena_t_ *a);

extern void ds_gc_destroy(void);

extern void ds_gc_unregister_allocation(void *ptr);

#endif
