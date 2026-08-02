#include <stdio.h>

#include "common.h"
#include "ds_arena.h"
#include "ds_string.h"

int main() {
  _ds_arena_t_ *arena = ds_arena_new(0);

  ds_string_t *text = ds_str_new(
      arena,
      "La fonction ds_str_find actuelle parcourt la chaîne caractère par caractère, ce qui provoque de nombreux échecs "
      "de prédiction de branchement (branch mispredictions). Avec AVX2, nous chargeons 32 caractères de la chaîne d'un "
      "coup et nous les comparons simultanément avec le premier caractère de la sous-chaîne recherchée en générant un "
      "masque de bits.Voici l'implémentation vectorielle de ds_str_find :#include <immintrin.h> int ds_str_find(const "
      "ds_string_t *src, const ds_string_t *sub) { if (!src || !sub || sub->length > src->length) return -1; if "
      "(sub->length == 0) return 0; const char *h_data = src->data; size_t h_len = src->length; const char *n_data = "
      "sub->data; size_t n_len = sub->length; // Premier caractère de la sous-chaîne à chercher char first_char = "
      "n_data[0]; On duplique ce caractère 32 fois dans un registre de 256 bits __m256i first_vec = "
      "_mm256_set1_epi8(first_char); size_t i = 0; On s'arrête à (h_len - n_len) pour ne pas déborder size_t limit = "
      "h_len - n_len; Boucle vectorielle par paquets de 32 caractères for (; i + 32 <= limit; i += 32) { Chargement de "
      "32 octets depuis la chaîne principale __m256i chunk = _mm256_loadu_si256((const __m256i *)(h_data + i)); "
      "Comparaison binaire : renvoie 0xFF pour chaque octet égal, 0x00 sinon __m256i cmp_res = "
      "_mm256_cmpeq_epi8(chunk, first_vec); // Convertit le résultat en un masque d'entier 32 bits (1 bit par octet) "
      "int mask = _mm256_movemask_epi8(cmp_res); Si le masque n'est pas nul, au moins un des 32 caractères correspond "
      "!  while (mask != 0) { Instruction matérielle CPU (__builtin_ctz) pour trouver l'index du bit à 1 le plus bas "
      "int bit_idx = __builtin_ctz(mask); size_t match_pos = i + bit_idx; Vérification de sécurité pour le reste de la "
      "sous-chaîne if (match_pos <= limit && memcmp(h_data + match_pos, n_data, n_len) == 0) { return (int)match_pos; "
      "// Correspondance totale trouvée !  } On efface le bit traité pour chercher la correspondance suivante dans ce "
      "même paquet mask &= (mask - 1); } } Boucle de secours scalaire classique pour la fin de la chaîne (< 32 octets) "
      "for (; i <= limit; i++) { if (h_data[i] == first_char && memcmp(h_data + i, n_data, n_len) == 0) { return "
      "(int)i; } } return -1; // Non trouvé }\0");

  ds_string_t *sub = ds_str_new(arena, "Chargement");
  int found = ds_str_find(text, sub);

  if (found != -1)
    printf("%s found in the big text.\n", sub->data);
  else
    printf("%s not found inf the big text.\n", sub->data);

  ds_string_t *old_sub = ds_str_new(arena, "fonction");
  ds_string_t *new_sub = ds_str_new(arena, "miguel");
  ds_string_t *replae = ds_str_replace(arena, text, old_sub, new_sub);
  printf("string replaced : %s\n", replae->data);

  printf("string length: %zu\n", text->length);
  printf("Original text :\n\t%s", text->data);

  ds_arena_destroy(arena);
}
