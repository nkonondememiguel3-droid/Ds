#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "gc.h"

static double get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

int main() {
  printf("==================================================\n");
  printf("  STARTING 10/10 INTRUSIVE COALESCING BENCHMARK   \n");
  printf("==================================================\n\n");

  _ds_arena_t_ arena = ds_arena_new(0);
  int *array = NULL;

  double start = get_time_ms();

  // Stress Test : 50 000 insertions provoquant des dizaines d'allocations de tailles exponentielles
  for (int i = 0; i < 50000; i++) {
    ds_da_push(&arena, array, i);
  }

  double end = get_time_ms();

  ds_arena_print_stats(&arena);
  printf("Total Execution Time: %.2f ms\n", end - start);

  ds_arena_destroy(&arena);
  return 0;
}
