#ifndef GC_H
#define GC_H

#include "common.h"

void ds_gc_register_root(_ds_arena_t_ *a, ds_node_t *var_ptr);

void ds_gc_register_allocation(void *ptr);

void ds_arena_run_gc(_ds_arena_t_ *a);

void ds_gc_destroy(void);

#endif
