/*
 * Small smoke-test / usage demo for the DS framework.
 */

#include <stdio.h>

#include "common.h"
#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_platform.h"
#include "ds_string.h"
#include "gc.h"

int main(void) {
  _ds_arena_t_ *arena = ds_arena_new(0);

  ds_string_t *text = ds_str_new(
      arena,
      "The scalar ds_str_find walks the haystack one byte at a time, which costs a branch misprediction on "
      "every near miss. With AVX2 we load 32 haystack bytes at once and compare them against a broadcast of "
      "the needle's first byte, turning the scan into a bitmask we can iterate with a count-trailing-zeros "
      "instruction. Only the positions the mask reports are worth a memcmp, so the expensive comparison runs "
      "a handful of times instead of once per byte. The vector loop is bounded by the haystack length rather "
      "than by the last legal match position, so a 32-byte load can never start close enough to the end of "
      "the buffer to read past it, and the leftover bytes are handed to the scalar routine.");

  ds_string_t *sub = ds_str_new(arena, "bitmask");
  ptrdiff_t found = ds_str_find(text, sub);

  printf("AVX2 available at runtime: %s\n", ds_cpu_has_avx2() ? "yes" : "no");

  if (found >= 0)
    printf("\"%s\" found at offset %td.\n", sub->data, found);
  else
    printf("\"%s\" not found in the text.\n", sub->data);

  {
    ds_string_t *old_sub = ds_str_new(arena, "scalar");
    ds_string_t *new_sub = ds_str_new(arena, "one-byte-at-a-time");
    ds_string_t *replaced = ds_str_replace(arena, text, old_sub, new_sub);
    printf("\nAfter replacement (%zu bytes):\n\t%s\n", replaced->length, replaced->data);
  }

  {
    ds_string_t **parts = ds_str_split(arena, ds_str_new(arena, "alpha,beta,gamma"), ds_str_new(arena, ","));
    size_t i;
    printf("\nSplit into %zu fields:", ds_da_len(parts));
    for (i = 0; i < ds_da_len(parts); i++) printf(" [%s]", parts[i]->data);
    printf("\n");
  }

  printf("\nOriginal text length: %zu\n", text->length);

  ds_arena_print_stats(arena);
  ds_arena_destroy(arena);
  return 0;
}
