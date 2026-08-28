#include "ds_string.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"

#if DS_HAVE_AVX2_INTRINSICS
#include <immintrin.h>
#endif

/* ------------------------------------------------------------------ */
/* GC integration                                                      */
/* ------------------------------------------------------------------ */

static void ds_string_finalize(void *ptr, _ds_arena_t_ *a) {
  ds_string_t *s = (ds_string_t *)ptr;
  if (s && s->data) {
    ds_arena_recycle_raw(a, s->data, ds_arena_align_up(s->length + 1));
    s->data = NULL;
    s->length = 0;
  }
}

const ds_type_descriptor_t ds_string_descriptor = {NULL, ds_string_finalize};

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

ds_string_t *ds_str_new(_ds_arena_t_ *a, const char *c_str) {
  if (!c_str) return NULL;
  return ds_str_new_len(a, c_str, strlen(c_str));
}

ds_string_t *ds_str_new_len(_ds_arena_t_ *a, const char *c_str, size_t len) {
  ds_string_t *str;
  size_t alloc_buffer_size;

  if (!a) return NULL;
  if (len == (size_t)-1) return NULL; /* len + 1 would wrap */

  /* The string object is managed (so the collector can reclaim it) but its
   * character buffer is raw and released by the finalizer. Allocating the
   * buffer as managed -- as the original did -- made the GC sweep it
   * independently of its owner, freeing live character data. */
  str = (ds_string_t *)ds_arena_alloc(a, sizeof(ds_string_t), &ds_string_descriptor);
  str->length = len;

  alloc_buffer_size = ds_arena_align_up(len + 1);
  str->data = (char *)ds_arena_alloc_raw(a, alloc_buffer_size);

  if (c_str && len > 0) memcpy(str->data, c_str, len);
  str->data[len] = '\0';

  return str;
}

ds_string_t *ds_str_dup(_ds_arena_t_ *a, const ds_string_t *src) {
  if (!src) return NULL;
  return ds_str_new_len(a, src->data, src->length);
}

ds_string_t *ds_str_concat(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *s2) {
  size_t new_len;
  ds_string_t *res;

  if (!s1) return ds_str_dup(a, s2);
  if (!s2) return ds_str_dup(a, s1);

  if (s1->length > (size_t)-2 - s2->length) return NULL;
  new_len = s1->length + s2->length;

  res = ds_str_new_len(a, NULL, new_len);
  if (!res) return NULL;

  if (s1->length) memcpy(res->data, s1->data, s1->length);
  if (s2->length) memcpy(res->data + s1->length, s2->data, s2->length);
  res->data[new_len] = '\0';

  return res;
}

/* ------------------------------------------------------------------ */
/* Comparison                                                          */
/* ------------------------------------------------------------------ */

bool ds_str_equal(const ds_string_t *s1, const ds_string_t *s2) {
  if (s1 == s2) return true;
  if (!s1 || !s2) return false;
  if (s1->length != s2->length) return false;
  if (s1->length == 0) return true;
  return memcmp(s1->data, s2->data, s1->length) == 0;
}

int ds_str_compare(const ds_string_t *s1, const ds_string_t *s2) {
  size_t min_len;
  int cmp;

  if (!s1 && !s2) return 0;
  if (!s1) return -1;
  if (!s2) return 1;

  min_len = (s1->length < s2->length) ? s1->length : s2->length;
  cmp = min_len ? memcmp(s1->data, s2->data, min_len) : 0;
  if (cmp != 0) return cmp;

  if (s1->length < s2->length) return -1;
  if (s1->length > s2->length) return 1;
  return 0;
}

ds_string_t *ds_str_substr(_ds_arena_t_ *a, const ds_string_t *src, size_t start, size_t len) {
  if (!src || start >= src->length) return ds_str_new(a, "");
  if (len > src->length - start) len = src->length - start;
  return ds_str_new_len(a, src->data + start, len);
}

/* ------------------------------------------------------------------ */
/* Substring search                                                    */
/* ------------------------------------------------------------------ */

/*
 * Scalar reference implementation. Also the fallback whenever AVX2 is not
 * available at compile time or at run time.
 */
static ptrdiff_t ds_str_find_scalar(const char *h, size_t h_len, const char *n, size_t n_len) {
  size_t limit = h_len - n_len; /* caller guarantees n_len <= h_len */
  size_t i;

  for (i = 0; i <= limit; i++) {
    const char *hit = (const char *)memchr(h + i, n[0], (limit - i) + 1);
    if (!hit) return -1;
    i = (size_t)(hit - h);
    if (memcmp(h + i, n, n_len) == 0) return (ptrdiff_t)i;
  }
  return -1;
}

#if DS_HAVE_AVX2_INTRINSICS
/*
 * AVX2 kernel: broadcast the needle's first byte across a 256-bit register,
 * compare 32 haystack bytes at a time, and only fall back to memcmp on the
 * candidate positions the mask reports.
 *
 * Bugs fixed relative to the original:
 *   - the vector loop bound was `i + 32 < limit` (limit = h_len - n_len), so
 *     a 32-byte load could start up to n_len bytes before the end and read
 *     past the buffer; it is now bounded by h_len.
 *   - the scalar tail was `for (; i <= 32; i++)`, i.e. it restarted at the
 *     wrong place, scanned a fixed 33 bytes regardless of string length,
 *     and read out of bounds on short strings.
 *   - the tail's comparison was `memcmp(h_data + i, n_data + i, n_len)`,
 *     which walked the needle forward in lockstep with the haystack and so
 *     compared the needle against itself past its own end.
 */
DS_TARGET_AVX2 static ptrdiff_t ds_str_find_avx2(const char *h, size_t h_len, const char *n, size_t n_len) {
  size_t limit = h_len - n_len;
  size_t i = 0;
  __m256i first_vec = _mm256_set1_epi8((char)n[0]);

  for (; i + 32 <= h_len; i += 32) {
    __m256i chunk = _mm256_loadu_si256((const __m256i *)(const void *)(h + i));
    __m256i cmp_res = _mm256_cmpeq_epi8(chunk, first_vec);
    uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp_res);

    while (mask != 0u) {
      unsigned bit_idx = ds_ctz32(mask);
      size_t match_position = i + bit_idx;

      if (match_position > limit) return -1; /* every later candidate is too */
      if (memcmp(h + match_position, n, n_len) == 0) return (ptrdiff_t)match_position;

      mask &= (mask - 1u); /* clear the low set bit and try the next one */
    }
  }

  if (i > limit) return -1;
  {
    ptrdiff_t tail = ds_str_find_scalar(h + i, h_len - i, n, n_len);
    return (tail < 0) ? -1 : (ptrdiff_t)i + tail;
  }
}
#endif /* DS_HAVE_AVX2_INTRINSICS */

ptrdiff_t ds_str_find(const ds_string_t *src, const ds_string_t *sub) {
  if (!src || !sub || !src->data || !sub->data) return -1;
  if (sub->length == 0) return 0;
  if (sub->length > src->length) return -1;

#if DS_HAVE_AVX2_INTRINSICS
  /* 32 bytes is the break-even point; below it the scalar path wins, and
   * the runtime probe keeps this from executing an illegal instruction on
   * a pre-Haswell CPU (the original called AVX2 unconditionally). */
  if (src->length >= 32 && ds_cpu_has_avx2()) return ds_str_find_avx2(src->data, src->length, sub->data, sub->length);
#endif

  return ds_str_find_scalar(src->data, src->length, sub->data, sub->length);
}

/* ------------------------------------------------------------------ */
/* Split / join                                                        */
/* ------------------------------------------------------------------ */

ds_string_t **ds_str_split(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *delim) {
  ds_string_t **res_array = NULL;
  ds_string_t remainder;

  if (!a || !s1) return NULL;

  if (!delim || delim->length == 0 || delim->length > s1->length) {
    ds_da_push(a, res_array, ds_str_dup(a, s1));
    return res_array;
  }

  remainder = *s1;

  for (;;) {
    ptrdiff_t match_idx = ds_str_find(&remainder, delim);
    size_t offset;

    if (match_idx < 0) {
      ds_da_push(a, res_array, ds_str_dup(a, &remainder));
      break;
    }

    ds_da_push(a, res_array, ds_str_substr(a, &remainder, 0, (size_t)match_idx));

    offset = (size_t)match_idx + delim->length;
    /* The original compared `offset >= remainder.length`, which dropped the
     * final empty field when the delimiter ended the string and, worse,
     * skipped a legitimate one-character trailing token. */
    if (offset > remainder.length) break;

    remainder.data += offset;
    remainder.length -= offset;

    if (remainder.length == 0) {
      ds_da_push(a, res_array, ds_str_new(a, ""));
      break;
    }
  }

  return res_array;
}

ds_string_t *ds_str_join(_ds_arena_t_ *a, ds_string_t **arr, const ds_string_t *sep) {
  size_t count, sep_len, total_length = 0, i;
  ds_string_t *res;
  char *dest;

  if (!a) return NULL;

  count = ds_da_len(arr);
  if (count == 0) return ds_str_new(a, "");
  if (count == 1) return ds_str_dup(a, arr[0]);

  sep_len = sep ? sep->length : 0;

  for (i = 0; i < count; i++) {
    if (arr[i]) total_length += arr[i]->length;
    if (i + 1 < count) total_length += sep_len;
  }

  res = ds_str_new_len(a, NULL, total_length);
  if (!res) return NULL;
  dest = res->data;

  for (i = 0; i < count; i++) {
    if (arr[i] && arr[i]->length > 0) {
      memcpy(dest, arr[i]->data, arr[i]->length);
      dest += arr[i]->length;
    }
    if (i + 1 < count && sep_len > 0) {
      memcpy(dest, sep->data, sep_len);
      dest += sep_len;
    }
  }
  *dest = '\0';

  return res;
}

/* ------------------------------------------------------------------ */
/* Formatting / trimming / replacement                                 */
/* ------------------------------------------------------------------ */

ds_string_t *ds_str_format(_ds_arena_t_ *a, const char *format, ...) {
  va_list args1, args2;
  int len;
  ds_string_t *res;

  if (!a || !format) return NULL;

  va_start(args1, format);
  va_copy(args2, args1);

  len = vsnprintf(NULL, 0, format, args1);
  va_end(args1);

  if (len < 0) {
    va_end(args2);
    return NULL;
  }

  res = ds_str_new_len(a, NULL, (size_t)len);
  if (!res) {
    va_end(args2);
    return NULL;
  }

  vsnprintf(res->data, (size_t)len + 1, format, args2);
  va_end(args2);

  return res;
}

ds_string_t *ds_str_trim(_ds_arena_t_ *a, const ds_string_t *src) {
  size_t start, end;

  if (!src || src->length == 0) return ds_str_new(a, "");

  start = 0;
  end = src->length;

  while (start < end && isspace((unsigned char)src->data[start])) start++;
  while (end > start && isspace((unsigned char)src->data[end - 1])) end--;

  return ds_str_new_len(a, src->data + start, end - start);
}

ds_string_t *ds_str_replace(_ds_arena_t_ *a, const ds_string_t *src, const ds_string_t *old_sub,
                            const ds_string_t *new_sub) {
  static char ds_empty_buf[1] = {'\0'};
  static ds_string_t ds_empty_string = {0, ds_empty_buf};

  size_t occurrences = 0;
  size_t search_offset = 0;
  size_t final_len, current_pos;
  ds_string_t *res;
  char *dest;

  if (!a || !src) return NULL;
  if (!old_sub || old_sub->length == 0) return ds_str_dup(a, src);
  if (!new_sub) new_sub = &ds_empty_string;

  /* Pass 1: count occurrences. */
  while (search_offset + old_sub->length <= src->length) {
    ds_string_t window;
    ptrdiff_t match_idx;

    window.data = src->data + search_offset;
    window.length = src->length - search_offset;

    match_idx = ds_str_find(&window, old_sub);
    if (match_idx < 0) break;

    occurrences++;
    search_offset += (size_t)match_idx + old_sub->length;
  }

  if (occurrences == 0) return ds_str_dup(a, src);

  /* Guard the length arithmetic: with a longer replacement this can
   * legitimately overflow on pathological input. */
  if (new_sub->length > old_sub->length) {
    size_t growth = new_sub->length - old_sub->length;
    if (occurrences > ((size_t)-2 - src->length) / (growth ? growth : 1)) return NULL;
  }
  final_len = src->length + (occurrences * new_sub->length) - (occurrences * old_sub->length);

  res = ds_str_new_len(a, NULL, final_len);
  if (!res) return NULL;

  dest = res->data;
  current_pos = 0;

  /* Pass 2: copy. */
  while (current_pos < src->length) {
    ds_string_t window;
    ptrdiff_t match_idx;

    window.data = src->data + current_pos;
    window.length = src->length - current_pos;

    match_idx = ds_str_find(&window, old_sub);
    if (match_idx < 0) {
      size_t remaining_bytes = src->length - current_pos;
      memcpy(dest, src->data + current_pos, remaining_bytes);
      dest += remaining_bytes;
      break;
    }

    if (match_idx > 0) {
      memcpy(dest, src->data + current_pos, (size_t)match_idx);
      dest += (size_t)match_idx;
    }

    if (new_sub->length > 0) {
      memcpy(dest, new_sub->data, new_sub->length);
      dest += new_sub->length;
    }

    current_pos += (size_t)match_idx + old_sub->length;
  }
  *dest = '\0';

  return res;
}
