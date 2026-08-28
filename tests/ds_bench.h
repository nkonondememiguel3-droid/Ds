#ifndef DS_BENCH_H
#define DS_BENCH_H

/*
 * Minimal benchmark harness shared by every tests/bench_*.c program.
 *
 * Every measurement is repeated and reported as a median with its observed
 * range, rather than as a single timing. A one-shot number cannot tell a
 * real 10% regression apart from ordinary run-to-run noise, which is what
 * the original benchmarks printed.
 *
 * Usage:
 *
 *     DS_BENCH_LOOP(rep) {
 *         ... set up ...
 *         ds_bench_start();
 *         ... work ...
 *         ds_bench_stop();
 *         ... tear down ...
 *     }
 *     ds_bench_report("bump allocation", OPS);
 */

#include <stdio.h>
#include <stdlib.h>

#include "ds_platform.h"

#ifndef DS_BENCH_REPS
#define DS_BENCH_REPS 7
#endif

static double ds_bench_samples[DS_BENCH_REPS];
static int ds_bench_count = 0;
static double ds_bench_t0 = 0.0;

/* Open a measured region. */
static void ds_bench_start(void) { ds_bench_t0 = ds_time_ms(); }

/* Close it and record the elapsed time as one sample. */
static void ds_bench_stop(void) {
  if (ds_bench_count < DS_BENCH_REPS) ds_bench_samples[ds_bench_count++] = ds_time_ms() - ds_bench_t0;
}

/* Repeat the body DS_BENCH_REPS times, resetting the sample set first. */
#define DS_BENCH_LOOP(rep) for (ds_bench_count = 0, (rep) = 0; (rep) < DS_BENCH_REPS; (rep)++)

static int ds_bench_cmp(const void *x, const void *y) {
  double a = *(const double *)x, b = *(const double *)y;
  return (a > b) - (a < b);
}

/* Print the median of the collected samples, the per-operation cost derived
 * from it, and the full observed range. */
static void ds_bench_report(const char *label, size_t ops) {
  double median;

  if (ds_bench_count == 0 || ops == 0) {
    printf("  %-40s (no samples)\n", label);
    return;
  }

  qsort(ds_bench_samples, (size_t)ds_bench_count, sizeof(double), ds_bench_cmp);
  median = ds_bench_samples[ds_bench_count / 2];

  printf("  %-40s %10zu ops %9.3f ms %9.1f ns/op   [%.3f - %.3f]\n", label, ops, median, median * 1e6 / (double)ops,
         ds_bench_samples[0], ds_bench_samples[ds_bench_count - 1]);
}

static void ds_bench_banner(const char *suite) {
  printf("================================================================================\n");
  printf(" %s benchmark   (median of %d runs)\n", suite, DS_BENCH_REPS);
  printf(" AVX2 substring search: %s%s\n", ds_cpu_has_avx2() ? "enabled" : "disabled",
         ds_cpu_has_avx2() ? "   (set DS_NO_AVX2=1 to compare the scalar path)" : "");
  printf("================================================================================\n");
  printf("  %-40s %10s %12s %12s   %s\n", "case", "ops", "median", "per op", "range (ms)");
}

static void ds_bench_footer(void) {
  printf("================================================================================\n\n");
}

/* Keep a computed value from being optimised away. */
static volatile size_t ds_bench_sink = 0;
static void ds_bench_keep(size_t v) { ds_bench_sink += v; }

#endif /* DS_BENCH_H */
