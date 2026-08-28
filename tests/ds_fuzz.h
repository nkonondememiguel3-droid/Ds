#ifndef DS_FUZZ_H
#define DS_FUZZ_H

/*
 * Shared plumbing for the tests/fuzz_*.c programs.
 *
 * Each fuzzer exposes the libFuzzer entry point:
 *
 *     int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
 *
 * so that a clang build with -fsanitize=fuzzer drives it with coverage
 * feedback. Because that toolchain is not available everywhere -- notably
 * not under MSVC -- this header also supplies a standalone main() that
 * feeds the same entry point from a deterministic PRNG. The standalone
 * driver is not coverage-guided, but it runs on every platform, under
 * ASan/UBSan, and in CI.
 *
 *     fuzz_string                 # default iterations, random seed
 *     fuzz_string 1000000         # iteration count
 *     fuzz_string 1000000 12345   # count and seed, for replay
 *
 * The seed is always printed, so any failure can be reproduced exactly.
 *
 * These fuzzers check invariants, not just absence of crashes. A fuzzer
 * that only asks "did it segfault" finds memory errors and nothing else;
 * each one here also compares the library against a reference model, so a
 * wrong answer fails just as loudly as a bad pointer.
 */

#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "ds_platform.h"

/* ------------------------------------------------------------------ */
/* Consuming the input byte stream                                     */
/* ------------------------------------------------------------------ */

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t pos;
} ds_fuzz_t;

static DS_INLINE void fz_init(ds_fuzz_t *f, const uint8_t *data, size_t size) {
  f->data = data;
  f->size = size;
  f->pos = 0;
}

static DS_INLINE int fz_empty(const ds_fuzz_t *f) { return f->pos >= f->size; }

static DS_INLINE uint8_t fz_u8(ds_fuzz_t *f) { return f->pos < f->size ? f->data[f->pos++] : 0u; }

static DS_INLINE uint32_t fz_u32(ds_fuzz_t *f) {
  uint32_t v = 0;
  int i;
  for (i = 0; i < 4; i++) v = (v << 8) | fz_u8(f);
  return v;
}

/* Inclusive range. Returns `lo` once the stream is exhausted, which keeps
 * every loop driven by this function terminating. */
static DS_INLINE size_t fz_range(ds_fuzz_t *f, size_t lo, size_t hi) {
  if (hi <= lo) return lo;
  return lo + (size_t)(fz_u32(f) % (uint32_t)(hi - lo + 1));
}

/* Copy up to `cap` bytes into `out`, returning how many were written. The
 * bytes may include embedded NULs -- ds_string_t is length-prefixed and has
 * to survive them. */
static DS_INLINE size_t fz_bytes(ds_fuzz_t *f, char *out, size_t cap) {
  size_t n = fz_range(f, 0, cap);
  size_t i;
  for (i = 0; i < n; i++) out[i] = (char)fz_u8(f);
  return i;
}

/* Same, but drawn from a small alphabet so that substring matches and hash
 * collisions actually happen instead of being astronomically unlikely. */
static DS_INLINE size_t fz_bytes_narrow(ds_fuzz_t *f, char *out, size_t cap) {
  static const char alphabet[] = "aab";
  size_t n = fz_range(f, 0, cap);
  size_t i;
  for (i = 0; i < n; i++) out[i] = alphabet[fz_u8(f) % (sizeof(alphabet) - 1)];
  return i;
}

/* ------------------------------------------------------------------ */
/* Failure reporting                                                   */
/* ------------------------------------------------------------------ */

extern const char *ds_fuzz_name;
static unsigned long long ds_fuzz_seed = 0;
static unsigned long long ds_fuzz_iter = 0;

#define FZ_CHECK(cond, msg)                                                              \
  do {                                                                                   \
    if (!(cond)) {                                                                       \
      fflush(stdout);                                                                    \
      fprintf(stderr,                                                                    \
              "\n%s: INVARIANT VIOLATED\n"                                               \
              "  %s\n"                                                                   \
              "  at %s:%d\n"                                                             \
              "  replay with: %s 1 %llu   (iteration %llu of this run)\n\n",             \
              ds_fuzz_name, (msg), __FILE__, __LINE__, ds_fuzz_name, ds_fuzz_seed,       \
              ds_fuzz_iter);                                                             \
      abort();                                                                           \
    }                                                                                    \
  } while (0)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* ------------------------------------------------------------------ */
/* Standalone driver                                                   */
/* ------------------------------------------------------------------ */

#ifndef DS_FUZZ_LIBFUZZER

#define DS_FUZZ_MAX_INPUT 4096
#ifndef DS_FUZZ_DEFAULT_ITERS
#define DS_FUZZ_DEFAULT_ITERS 20000
#endif

/* xorshift64*, so a seed reproduces a run exactly on every platform
 * regardless of what rand() happens to do there. */
static uint64_t ds_fuzz_rng_state = 1;

static uint64_t ds_fuzz_rng(void) {
  uint64_t x = ds_fuzz_rng_state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  ds_fuzz_rng_state = x;
  return x * UINT64_C(0x2545F4914F6CDD1D);
}

int main(int argc, char **argv) {
  static uint8_t buf[DS_FUZZ_MAX_INPUT];
  unsigned long long iters = DS_FUZZ_DEFAULT_ITERS;
  unsigned long long i;

  if (argc > 1) iters = strtoull(argv[1], NULL, 10);
  if (argc > 2)
    ds_fuzz_seed = strtoull(argv[2], NULL, 10);
  else
    ds_fuzz_seed = (unsigned long long)time(NULL) ^ (unsigned long long)(uintptr_t)&buf[0];

  ds_fuzz_rng_state = ds_fuzz_seed ? ds_fuzz_seed : 1;

  printf("%s: %llu iterations, seed %llu\n", ds_fuzz_name, iters, ds_fuzz_seed);

  for (i = 0; i < iters; i++) {
    size_t len = (size_t)(ds_fuzz_rng() % DS_FUZZ_MAX_INPUT);
    size_t j;
    ds_fuzz_iter = i;
    for (j = 0; j < len; j += 8) {
      uint64_t r = ds_fuzz_rng();
      size_t k;
      for (k = 0; k < 8 && j + k < len; k++) buf[j + k] = (uint8_t)(r >> (8 * k));
    }
    LLVMFuzzerTestOneInput(buf, len);
  }

  printf("%s: %llu iterations, no invariant violations\n", ds_fuzz_name, iters);
  return 0;
}

#endif /* DS_FUZZ_LIBFUZZER */

#endif /* DS_FUZZ_H */
