#include "ds_platform.h"

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Monotonic clock                                                     */
/* ------------------------------------------------------------------ */

#if DS_OS_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>

double ds_time_ms(void) {
  static LARGE_INTEGER freq;
  static int freq_ready = 0;
  LARGE_INTEGER now;

  if (!freq_ready) {
    QueryPerformanceFrequency(&freq);
    freq_ready = 1;
  }
  QueryPerformanceCounter(&now);
  return ((double)now.QuadPart * 1000.0) / (double)freq.QuadPart;
}

#else /* POSIX */

/* clock_gettime + CLOCK_MONOTONIC come from POSIX.1-2001; glibc only
 * declares them when a feature-test macro is set before any libc header.
 * Declaring it here (rather than in each benchmark) keeps the requirement
 * in exactly one place. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <time.h>

#if defined(CLOCK_MONOTONIC)

double ds_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
}

#else /* very old systems: fall back to the C89 process clock */

double ds_time_ms(void) { return ((double)clock() * 1000.0) / (double)CLOCKS_PER_SEC; }

#endif
#endif /* DS_OS_WINDOWS */

/* ------------------------------------------------------------------ */
/* CPU feature detection                                               */
/* ------------------------------------------------------------------ */

#if DS_ARCH_X86

#if DS_COMPILER_MSVC
#include <intrin.h>
static void ds_cpuid_count(int regs[4], int leaf, int subleaf) { __cpuidex(regs, leaf, subleaf); }
static uint64_t ds_xgetbv0(void) { return _xgetbv(0); }
#else
#include <cpuid.h>
static void ds_cpuid_count(int regs[4], int leaf, int subleaf) {
  unsigned int a = 0, b = 0, c = 0, d = 0;
  __cpuid_count((unsigned int)leaf, (unsigned int)subleaf, a, b, c, d);
  regs[0] = (int)a;
  regs[1] = (int)b;
  regs[2] = (int)c;
  regs[3] = (int)d;
}
static uint64_t ds_xgetbv0(void) {
  uint32_t lo = 0, hi = 0;
  __asm__ __volatile__(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0));
  return ((uint64_t)hi << 32) | lo;
}
#endif

int ds_cpu_has_avx2(void) {
  static int cached = -1; /* -1 = not probed yet */
  int regs[4];
  int max_leaf;
  int osxsave, avx;
  uint64_t xcr0;

  if (cached >= 0) return cached;
  cached = 0;

  /* Escape hatch: setting DS_NO_AVX2 in the environment forces the scalar
   * paths. Useful for bisecting a suspected SIMD bug, and for exercising
   * the fallback in a test run on a machine that does have AVX2. */
  if (getenv("DS_NO_AVX2") != NULL) return cached;

  ds_cpuid_count(regs, 0, 0);
  max_leaf = regs[0];
  if (max_leaf < 7) return cached;

  /* Leaf 1: ECX bit 27 = OSXSAVE, ECX bit 28 = AVX */
  ds_cpuid_count(regs, 1, 0);
  osxsave = (regs[2] & (1 << 27)) != 0;
  avx = (regs[2] & (1 << 28)) != 0;
  if (!osxsave || !avx) return cached;

  /* The OS must actually be saving XMM (bit 1) and YMM (bit 2) state,
   * otherwise using AVX registers silently corrupts them across a context
   * switch. This is the check the original code was missing entirely. */
  xcr0 = ds_xgetbv0();
  if ((xcr0 & 0x6u) != 0x6u) return cached;

  /* Leaf 7, subleaf 0: EBX bit 5 = AVX2 */
  ds_cpuid_count(regs, 7, 0);
  cached = (regs[1] & (1 << 5)) != 0;
  return cached;
}

#else /* non-x86: ARM, RISC-V, ... */

int ds_cpu_has_avx2(void) { return 0; }

#endif /* DS_ARCH_X86 */
