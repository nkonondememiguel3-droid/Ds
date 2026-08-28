#ifndef DS_TEST_H
#define DS_TEST_H

/*
 * Minimal test harness shared by every tests/test_*.c program.
 *
 * Each suite is its own executable and its own ctest entry, so a failure
 * names the structure it came from and one broken suite does not hide the
 * others. Everything here is header-only; there is nothing to link.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ds_checks_run = 0;
static int ds_checks_failed = 0;
static const char *ds_suite = "?";

/* Assert `cond`, attributing the failure to `msg` and this source line. */
#define CHECK(cond, msg)                                                   \
  do {                                                                     \
    ds_checks_run++;                                                       \
    if (!(cond)) {                                                         \
      ds_checks_failed++;                                                  \
      printf("  FAIL  %s\n        at %s:%d\n", (msg), __FILE__, __LINE__); \
    }                                                                      \
  } while (0)

/* Group related checks under a heading. */
#define SECTION(name) printf("\n  -- %s\n", (name))

static void ds_test_begin(const char *suite) {
  ds_suite = suite;
  printf("== %s ==\n", suite);
}

static int ds_test_end(void) {
  printf("\n%-22s %3d checks, %d failed\n", ds_suite, ds_checks_run, ds_checks_failed);
  if (ds_checks_failed == 0) return EXIT_SUCCESS;
  printf("SUITE FAILED: %s\n", ds_suite);
  return EXIT_FAILURE;
}

#endif /* DS_TEST_H */
