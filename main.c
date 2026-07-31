#include <stdio.h>

#include "ds_arena.h"
#include "ds_string.h"
#include "gc.h"

int main() {
  _ds_arena_t_ arena = ds_arena_new(0);

  ds_string_t *lang = ds_str_new(&arena, "Lisp");
  int score = 99;
  ds_string_t *formatted = ds_str_format(&arena, "Le score de %s est de %d/100.", lang->data, score);
  printf("Format : %s\n", formatted->data);

  ds_string_t *dirty_str = ds_str_new(&arena, "   \t  Texte Nettoye    \n ");
  ds_string_t *clean_str = ds_str_trim(&arena, dirty_str);
  printf("Trim   : [%s]\n", clean_str->data);

  ds_string_t *phrase = ds_str_new(&arena, "Le C est rapide, le C est bas niveau.");
  ds_string_t *old_word = ds_str_new(&arena, "C");
  ds_string_t *new_word = ds_str_new(&arena, "Rust");

  ds_string_t *updated_phrase = ds_str_replace(&arena, phrase, old_word, new_word);
  printf("Replace: %s\n", updated_phrase->data);

  ds_arena_print_stats(&arena);

  ds_arena_run_gc(&arena);
  ds_arena_destroy(&arena);
  ds_gc_destroy();

  printf("\nTest des extensions utilitaires acheve avec succes !\n");
  return 0;
}
