#ifndef DS_VALUE_H
#define DS_VALUE_H

/*
 * The tagged value word.
 *
 * This is the representation layer, and it is deliberately free of any
 * dependency on the arena or the collector: it is nothing but an encoding
 * plus the inline functions that pack and unpack it. The arena does not
 * include this header at all -- it allocates bytes and has no opinion about
 * what is stored in them. The collector does, because a ds_node_t is what a
 * root points at and what a mark callback hands back.
 *
 * ds_node_t is a fixed 64-bit word, NOT uintptr_t. The original typedef was
 * uintptr_t, which works on LP64 but silently corrupts data on any 32-bit
 * target: ds_make_float shifts a 32-bit IEEE-754 pattern left by
 * DS_TAG_BITS, so the top four bits of every float fell off the end of a
 * 32-bit word, and ds_make_int had the same problem for large integers. A
 * fixed 64-bit word holds a pointer on every supported target and leaves
 * room for the tag on all of them.
 *
 * The pointer forms rely on their referent being aligned to at least
 * (1 << DS_TAG_BITS) bytes, so that the low bits are free to carry the tag.
 * Nothing here can enforce that; the allocator handing out the memory has
 * to. gc.h asserts that the arena's ARENA_ALIGN satisfies it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ds_platform.h"

#define DS_TAG_BITS 4

typedef enum {
  TYPE_INT = 0,
  TYPE_FLOAT = 1,
  TYPE_BOOL = 2,
  TYPE_NIL = 3,
  TYPE_STRING = 4,
  TYPE_NODE = 5,
} ds_type_t;

#define ARENA_TAG_MASK ((uint64_t)((1u << DS_TAG_BITS) - 1u))
#define ARENA_PTR_MASK (~ARENA_TAG_MASK)

typedef uint64_t ds_node_t;

DS_STATIC_ASSERT(TYPE_NODE <= ARENA_TAG_MASK, "every ds_type_t must fit in the tag bits");

static DS_INLINE ds_type_t ds_get_type(ds_node_t n) { return (ds_type_t)(n & ARENA_TAG_MASK); }

static DS_INLINE void *ds_get_ptr(ds_node_t n) { return (void *)(uintptr_t)(n & ARENA_PTR_MASK); }

static DS_INLINE ds_node_t ds_tag_ptr(void *ptr, ds_type_t type) {
  return ((ds_node_t)(uintptr_t)ptr) | (ds_node_t)type;
}

/* Left-shifting a negative signed value is undefined behaviour, so the shift
 * is done on the unsigned word and only the unpack reinterprets the sign. */
static DS_INLINE ds_node_t ds_make_int(int val) {
  return (((ds_node_t)(uint32_t)val) << DS_TAG_BITS) | (ds_node_t)TYPE_INT;
}

static DS_INLINE int ds_unpack_int(ds_node_t node) { return (int)(uint32_t)(node >> DS_TAG_BITS); }

static DS_INLINE ds_node_t ds_make_float(float val) {
  uint32_t bits;
  memcpy(&bits, &val, sizeof(bits));
  return (((ds_node_t)bits) << DS_TAG_BITS) | (ds_node_t)TYPE_FLOAT;
}

static DS_INLINE float ds_unpack_float(ds_node_t node) {
  uint32_t bits = (uint32_t)(node >> DS_TAG_BITS);
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

static DS_INLINE ds_node_t ds_make_bool(bool val) {
  return (((ds_node_t)(val ? 1u : 0u)) << DS_TAG_BITS) | (ds_node_t)TYPE_BOOL;
}

static DS_INLINE bool ds_unpack_bool(ds_node_t node) { return ((node >> DS_TAG_BITS) & 1u) != 0u; }

static DS_INLINE ds_node_t ds_make_nil(void) { return (ds_node_t)TYPE_NIL; }

#endif /* DS_VALUE_H */
