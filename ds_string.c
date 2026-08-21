#include "ds_string.h"

#include <ctype.h>
#include <immintrin.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"

ds_string_t *ds_str_new(_ds_arena_t_ *a, const char *c_str) {
  if (!c_str) return NULL;
  return ds_str_new_len(a, c_str, strlen(c_str));
}

ds_string_t *ds_str_new_len(_ds_arena_t_ *a, const char *c_str, size_t len) {
  ds_string_t *str = (ds_string_t *)ds_arena_alloc(a, sizeof(ds_string_t));
  str->length = len;

  size_t alloc_buffer_size = ds_arena_align_up(len + 1);
  str->data = (char *)ds_arena_alloc(a, alloc_buffer_size);

  if (c_str) {
    memcpy(str->data, c_str, len);
  }
  str->data[len] = '\0';

  return str;
}

ds_string_t *ds_str_dup(_ds_arena_t_ *a, const ds_string_t *src) {
  if (!src) return NULL;
  return ds_str_new_len(a, src->data, src->length);
}

ds_string_t *ds_str_concat(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *s2) {
  if (!s1) return ds_str_dup(a, s2);
  if (!s2) return ds_str_dup(a, s1);

  size_t new_len = s1->length + s2->length;
  ds_string_t *res = ds_str_new_len(a, NULL, new_len);

  memcpy(res->data, s1->data, s1->length);
  memcpy(res->data + s1->length, s2->data, s2->length);

  return res;
}

bool ds_str_equal(const ds_string_t *s1, const ds_string_t *s2) {
  if (s1 == s2) return true;
  if (!s1 || !s2) return false;
  if (s1->length != s2->length) return false;
  return memcmp(s1->data, s2->data, s1->length) == 0;
}

int ds_str_compare(const ds_string_t *s1, const ds_string_t *s2) {
  if (!s1 && !s2) return 0;
  if (!s1) return -1;
  if (!s2) return 1;

  size_t min_len = (s1->length < s2->length) ? s1->length : s2->length;
  int cmp = memcmp(s1->data, s2->data, min_len);
  if (cmp != 0) return cmp;

  if (s1->length < s2->length) return -1;
  if (s1->length > s2->length) return 1;
  return 0;
}

ds_string_t *ds_str_substr(_ds_arena_t_ *a, const ds_string_t *src, size_t start, size_t len) {
  if (!src || start >= src->length) return ds_str_new(a, "");
  if (start + len > src->length) {
    len = src->length - start;
  }
  return ds_str_new_len(a, src->data + start, len);
}

/* int ds_str_find(const ds_string_t *src, const ds_string_t *sub) { */
/*   if (!src || !sub || sub->length > src->length) return -1; */
/*   if (sub->length == 0) return 0; */

/*   size_t limit = src->length - sub->length; */
/*   for (size_t i = 0; i <= limit; i++) { */
/*     if (memcmp(src->data + i, sub->data, sub->length) == 0) { */
/*       return (int)i; */
/*     } */
/*   } */
/*   return -1; */
/* } */
int ds_str_find(const ds_string_t *src, const ds_string_t *sub) {
  if (!src || !sub || sub->length > src->length) return -1;
  if (sub->length == 0) return 0;

  const char *h_data = src->data;
  size_t h_len = src->length;
  const char *n_data = sub->data;
  size_t n_len = sub->length;

  /* first character of the sub string to find */
  char first_char = n_data[0];
  /* we duplicate this character 32 time of a 256-bit register */
  __m256i first_vec = _mm256_set1_epi8(first_char);

  size_t i = 0;
  size_t limit = h_len - n_len;

  for (; i + 32 < limit; i += 32) {
    __m256i chunk = _mm256_loadu_si256((const __m256i *)(h_data + i));

    /* binary comparison: return 0xFF if there are equal otherwise it returns 0x00 */
    __m256i cmp_res = _mm256_cmpeq_epi8(chunk, first_vec);
    /* convert to a mask of 32-bit ones(1) */
    int mask = _mm256_movemask_epi8(cmp_res);

    while (mask != 0) {
      int bit_idx = __builtin_ctz(mask);
      size_t match_position = i + bit_idx;

      if (match_position <= limit && memcmp(h_data + match_position, n_data, n_len) == 0)
        return (int)match_position; /* find */

      /* clear the bit to search the correspondance in the same package */
      mask &= (mask - 1);
    }
  }

  for (; i <= 32; i++)
    if (h_data[i] == first_char && memcmp(h_data + i, n_data + i, n_len) == 0) return (int)i;

  /* not found */
  return -1;
}

ds_string_t **ds_str_split(_ds_arena_t_ *a, const ds_string_t *s1, const ds_string_t *delim) {
  if (!s1) return NULL;

  ds_string_t **res_array = NULL;

  if (!delim || delim->length == 0 || delim->length > s1->length) {
    ds_da_push(a, res_array, ds_str_dup(a, s1));
    return res_array;
  }

  size_t current_pos = 0;
  ds_string_t remainder = *s1;

  while (remainder.length > 0) {
    int match_idx = ds_str_find(&remainder, delim);
    if (match_idx == -1) {
      ds_da_push(a, res_array, ds_str_dup(a, &remainder));
      break;
    }

    ds_string_t *token = ds_str_substr(a, &remainder, 0, (size_t)match_idx);
    ds_da_push(a, res_array, token);

    // Avancer après le délimiteur
    size_t offset = (size_t)match_idx + delim->length;
    if (offset >= remainder.length) {
      ds_da_push(a, res_array, ds_str_new(a, ""));
      break;
    }

    remainder.data += offset;
    remainder.length -= offset;
  }

  return res_array;
}

ds_string_t *ds_str_join(_ds_arena_t_ *a, ds_string_t **arr, const ds_string_t *sep) {
  size_t count = ds_da_len(arr);
  if (count == 0) return ds_str_new(a, "");
  if (count == 1) return ds_str_dup(a, arr[0]);

  size_t sep_len = sep ? sep->length : 0;
  size_t total_length = 0;

  for (size_t i = 0; i < count; i++) {
    if (arr[i]) {
      total_length += arr[i]->length;
    }
    if (i < count - 1) {
      total_length += sep_len;
    }
  }

  ds_string_t *res = ds_str_new_len(a, NULL, total_length);
  char *dest = res->data;

  for (size_t i = 0; i < count; i++) {
    if (arr[i] && arr[i]->length > 0) {
      memcpy(dest, arr[i]->data, arr[i]->length);
      dest += arr[i]->length;
    }
    if (i < count - 1 && sep_len > 0) {
      memcpy(dest, sep->data, sep_len);
      dest += sep_len;
    }
  }
  *dest = '\0';

  return res;
}

ds_string_t *ds_str_format(_ds_arena_t_ *a, const char *format, ...) {
  if (!format) return NULL;

  va_list args1, args2;
  va_start(args1, format);
  va_copy(args2, args1);

  // 1. Premier passage : Calculer la taille exacte requise (sans le \0)
  int len = vsnprintf(NULL, 0, format, args1);
  va_end(args1);

  if (len < 0) {
    va_end(args2);
    return NULL;
  }

  // 2. Allocation unique de la bonne taille dans l'arène
  ds_string_t *res = ds_str_new_len(a, NULL, (size_t)len);

  // 3. Deuxième passage : Écriture finale dans le buffer de l'arène
  vsnprintf(res->data, (size_t)len + 1, format, args2);
  va_end(args2);

  return res;
}

ds_string_t *ds_str_trim(_ds_arena_t_ *a, const ds_string_t *src) {
  if (!src || src->length == 0) return ds_str_new(a, "");

  size_t start = 0;
  size_t end = src->length;

  // Avancer le pointeur de début tant qu'on croise des espaces
  while (start < end && isspace((unsigned char)src->data[start])) {
    start++;
  }

  // Reculer le pointeur de fin tant qu'on croise des espaces
  while (end > start && isspace((unsigned char)src->data[end - 1])) {
    end--;
  }

  size_t new_len = end - start;
  return ds_str_new_len(a, src->data + start, new_len);
}

ds_string_t *ds_str_replace(_ds_arena_t_ *a, const ds_string_t *src, const ds_string_t *old_sub,
                            const ds_string_t *new_sub) {
  if (!src || !old_sub || old_sub->length == 0) return ds_str_dup(a, src);
  if (!new_sub) {
    static ds_string_t empty = {0, ""};
    new_sub = &empty;
  }

  size_t occurrences = 0;
  size_t search_offset = 0;

  while (search_offset + old_sub->length <= src->length) {
    ds_string_t window;
    window.data = src->data + search_offset;
    window.length = src->length - search_offset;

    int match_idx = ds_str_find(&window, old_sub);
    if (match_idx == -1) break;

    occurrences++;
    search_offset += (size_t)match_idx + old_sub->length;
  }

  if (occurrences == 0) return ds_str_dup(a, src);

  size_t final_len = src->length + (occurrences * new_sub->length) - (occurrences * old_sub->length);
  ds_string_t *res = ds_str_new_len(a, NULL, final_len);

  char *dest = res->data;
  size_t current_pos = 0;

  while (current_pos < src->length) {
    ds_string_t window;
    window.data = src->data + current_pos;
    window.length = src->length - current_pos;

    int match_idx = ds_str_find(&window, old_sub);
    if (match_idx == -1) {
      size_t remaining_bytes = src->length - current_pos;
      memcpy(dest, src->data + current_pos, remaining_bytes);
      dest += remaining_bytes;
      break;
    }

    if (match_idx > 0) {
      memcpy(dest, src->data + current_pos, (size_t)match_idx);
      dest += match_idx;
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
