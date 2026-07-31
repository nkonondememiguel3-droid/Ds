#ifndef ds_string_h
#define ds_string_h

#include <stdarg.h>
#include <stdbool.h>

#include "common.h"

typedef struct ALIGN16 {
  size_t length;
  char *data;
} ALIGN16_POST ds_string_t;

extern ds_string_t *ds_str_new(_ds_arena_t_ *a, const char *c_str);
extern ds_string_t *ds_str_new_len(_ds_arena_t_ *a, const char *c_str, size_t len);
extern ds_string_t *ds_str_dup(_ds_arena_t_ *a, const ds_string_t *src);

extern ds_string_t *ds_str_concat(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *s2);
extern bool ds_str_equal(const ds_string_t *s1, const ds_string_t *s2);
extern int ds_str_compare(const ds_string_t *s1, const ds_string_t *s2);
extern ds_string_t *ds_str_substr(_ds_arena_t_ *a, const ds_string_t *src, size_t start, size_t len);
extern int ds_str_find(const ds_string_t *src, const ds_string_t *sub);
extern ds_string_t **ds_str_split(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *delim);
extern ds_string_t *ds_str_join(_ds_arena_t_ *a, ds_string_t **arr, const ds_string_t *sep);
extern ds_string_t *ds_str_format(_ds_arena_t_ *a, const char *format, ...);
extern ds_string_t *ds_str_trim(_ds_arena_t_ *a, const ds_string_t *src);
extern ds_string_t *ds_str_replace(_ds_arena_t_ *a, const ds_string_t *src, const ds_string_t *old_sub,
                                   const ds_string_t *new_sub);

#endif  // ds_string_h
