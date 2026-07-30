#include <stdio.h>

#include "ds_arena.h"
#include "gc.h"

typedef struct {
  ds_node_t value;
  ds_node_t next;
} MyListNode;

int main() {
  _ds_arena_t_ arena = ds_arena_new(0);

  ds_node_t ma_liste = ds_tag_ptr(NULL, TYPE_NIL);

  ds_gc_register_root(&arena, &ma_liste);

  MyListNode *n1 = ARENA_NEW(&arena, MyListNode);
  n1->value = ds_tag_ptr((void *)42, TYPE_INT);
  n1->next = ds_tag_ptr(NULL, TYPE_NIL);
  ma_liste = ds_tag_ptr(n1, TYPE_NODE);

  MyListNode *n2 = ARENA_NEW(&arena, MyListNode);
  n2->value = ds_tag_ptr((void *)100, TYPE_INT);
  n2->next = ma_liste;
  ma_liste = ds_tag_ptr(n2, TYPE_NODE);

  printf("Liste initiale créée dans l'arène.\n");

  ma_liste = n2->next;

  ds_arena_run_gc(&arena);

  MyListNode *n3 = ARENA_NEW(&arena, MyListNode);
  printf("Nouveau nœud alloué à l'adresse recyclée : %p\n", (void *)n3);

  ds_arena_destroy(&arena);
  ds_gc_destroy();
  return 0;
}
