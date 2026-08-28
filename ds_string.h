#ifndef ds_string_h
#define ds_string_h

#include <stdbool.h>
#include <stddef.h>

#include "common.h"
#include "ds_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  DS_ALIGNAS(ARENA_ALIGN) size_t length;
  char *data; /* NUL-terminated, but `length` is authoritative */
} ds_string_t;

/* Descriptor for GC-managed strings: no outgoing references, but the
 * character buffer is a raw allocation that has to be handed back when the
 * string object itself is swept. */
extern const ds_type_descriptor_t ds_string_descriptor;

ds_string_t *ds_str_new(_ds_arena_t_ *a, const char *c_str);
ds_string_t *ds_str_new_len(_ds_arena_t_ *a, const char *c_str, size_t len);
ds_string_t *ds_str_dup(_ds_arena_t_ *a, const ds_string_t *src);

ds_string_t *ds_str_concat(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *s2);
bool ds_str_equal(const ds_string_t *s1, const ds_string_t *s2);
int ds_str_compare(const ds_string_t *s1, const ds_string_t *s2);
ds_string_t *ds_str_substr(_ds_arena_t_ *a, const ds_string_t *src, size_t start, size_t len);

/*
 * Index of the first occurrence of `sub` in `src`, or -1.
 *
 * Returns ptrdiff_t rather than int: an int return silently truncated the
 * index for strings larger than 2 GiB, turning a valid match into a bogus
 * (possibly negative) offset.
 */
ptrdiff_t ds_str_find(const ds_string_t *src, const ds_string_t *sub);

/* Returns a ds_da_* dynamic array of ds_string_t*; free with ds_da_free. */
ds_string_t **ds_str_split(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *delim);
ds_string_t *ds_str_join(_ds_arena_t_ *a, ds_string_t **arr, const ds_string_t *sep);

ds_string_t *ds_str_format(_ds_arena_t_ *a, const char *format, ...);
ds_string_t *ds_str_trim(_ds_arena_t_ *a, const ds_string_t *src);
ds_string_t *ds_str_replace(_ds_arena_t_ *a, const ds_string_t *src, const ds_string_t *old_sub,
                            const ds_string_t *new_sub);

#ifdef __cplusplus
}
#endif

#endif /* ds_string_h */
