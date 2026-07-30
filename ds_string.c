#include "ds_string.h"

#include <ctype.h>
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

int ds_str_find(const ds_string_t *src, const ds_string_t *sub) {
  if (!src || !sub || sub->length > src->length) return -1;
  if (sub->length == 0) return 0;

  size_t limit = src->length - sub->length;
  for (size_t i = 0; i <= limit; i++) {
    if (memcmp(src->data + i, sub->data, sub->length) == 0) {
      return (int)i;
    }
  }
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
  if (!new_sub) new_sub = ds_str_new(a, "");

  // 1. Premier passage : Compter le nombre d'occurrences pour calculer la taille finale
  size_t occurrences = 0;
  int search_idx = 0;
  ds_string_t remainder = *src;

  while ((search_idx = ds_str_find(&remainder, old_sub)) != -1) {
    occurrences++;
    size_t offset = (size_t)search_idx + old_sub->length;
    if (offset >= remainder.length) break;
    remainder.data += offset;
    remainder.length -= offset;
  }

  if (occurrences == 0) return ds_str_dup(a, src);

  // Calcul de la taille de la nouvelle chaîne
  size_t final_len = src->length + (occurrences * new_sub->length) - (occurrences * old_sub->length);
  ds_string_t *res = ds_str_new_len(a, NULL, final_len);

  // 2. Deuxième passage : Copier les segments et injecter les remplacements
  char *dest = res->data;
  remainder = *src;

  while (remainder.length > 0) {
    search_idx = ds_str_find(&remainder, old_sub);
    if (search_idx == -1) {
      // Copier le résidu final
      memcpy(dest, remainder.data, remainder.length);
      dest += remainder.length;
      break;
    }

    // Copier la partie avant la correspondance
    if (search_idx > 0) {
      memcpy(dest, remainder.data, (size_t)search_idx);
      dest += search_idx;
    }

    // Injecter la nouvelle sous-chaîne
    if (new_sub->length > 0) {
      memcpy(dest, new_sub->data, new_sub->length);
      dest += new_sub->length;
    }

    // Avancer au-delà du délimiteur remplacé
    size_t offset = (size_t)search_idx + old_sub->length;
    if (offset >= remainder.length) break;
    remainder.data += offset;
    remainder.length -= offset;
  }
  *dest = '\0';

  return res;
}
