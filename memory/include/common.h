#ifndef COMMON_H
#define COMMON_H

/*
 * Compatibility umbrella.
 *
 * What used to live here has been split along the line between the two
 * libraries:
 *
 *   ds_value.h   the tagged ds_node_t word and its accessors (no allocator)
 *   ds_arena.h   ARENA_ALIGN, block headers, chunks, the arena struct
 *   gc.h         type descriptors, roots, collector state
 *
 * Including this header still gets you all three, so existing sources keep
 * compiling -- but it also drags the collector in. New code that only wants
 * the arena should include ds_arena.h directly and link libds_arena.a alone.
 */

#include "ds_arena.h"
#include "ds_platform.h"
#include "ds_value.h"
#include "gc.h"

#endif
