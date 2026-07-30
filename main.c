#include <stdio.h>

#include "ds_arena.h"
#include "ds_stack_queue.h"
#include "gc.h"

int main() {
  _ds_arena_t_ arena = ds_arena_new(0);
  ds_stack_t *pile = ds_stack_new(&arena);

  printf("Lancement d'un scénario de stress mémoire (Push/Pop intensif)...\n");

  // Phase 1 : 10 allocations initiales (vont toutes consommer sur le Bump pointer)
  for (int i = 0; i < 10; i++) {
    ds_stack_push(&arena, pile, ds_make_int(i));
  }

  // Phase 2 : On vide entièrement la pile pour remplir la Free-List de blocs morts
  while (ds_stack_size(pile) > 0) {
    ds_stack_pop(&arena, pile);
  }

  // Phase 3 : On réinsère 10 nouveaux éléments.
  // L'arène doit intercepter la Free-List et afficher 100% de recyclage sur cette phase.
  for (int i = 0; i < 10; i++) {
    ds_stack_push(&arena, pile, ds_make_int(i * 10));
  }

  // 5. Affichage du rapport avant fermeture
  ds_arena_print_stats(&arena);

  // Nettoyage final
  ds_arena_run_gc(&arena);
  ds_arena_destroy(&arena);
  ds_gc_destroy();
  return 0;
}
