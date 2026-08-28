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

int ds_cpu_has_avx2(void) {
  static int cached = -1; /* -1 = not probed yet */
  int regs[4];
  int osxsave, avx;
  unsigned __int64 xcr0;

  if (cached >= 0) return cached;
  cached = 0;

  /* Escape hatch: setting DS_NO_AVX2 in the environment forces the scalar
   * paths. Useful for bisecting a suspected SIMD bug, and for exercising
   * the fallback in a test run on a machine that does have AVX2. */
  if (getenv("DS_NO_AVX2") != NULL) return cached;

  __cpuid(regs, 0);
  if (regs[0] < 7) return cached;

  /* Leaf 1: ECX bit 27 = OSXSAVE, ECX bit 28 = AVX */
  __cpuidex(regs, 1, 0);
  osxsave = (regs[2] & (1 << 27)) != 0;
  avx = (regs[2] & (1 << 28)) != 0;
  if (!osxsave || !avx) return cached;

  /* The OS must actually be saving XMM (bit 1) and YMM (bit 2) state,
   * otherwise using AVX registers silently corrupts them across a context
   * switch. */
  xcr0 = _xgetbv(0);
  if ((xcr0 & 0x6u) != 0x6u) return cached;

  /* Leaf 7, subleaf 0: EBX bit 5 = AVX2 */
  __cpuidex(regs, 7, 0);
  cached = (regs[1] & (1 << 5)) != 0;
  return cached;
}

#else /* GCC, Clang, MinGW-w64 */

/*
 * GCC and Clang already implement this entire probe -- the CPUID leaves,
 * the OSXSAVE bit and the XGETBV check for YMM state -- inside
 * __builtin_cpu_supports, so we let them do it.
 *
 * The previous version hand-rolled the CPUID sequence with __cpuid_count
 * and a small block of inline asm. Clang compiled it into
 *
 *     mov  $0x7, %eax
 *     xchg %rax, %rbx     ; stash RBX in RAX
 *     cpuid               ; ...but CPUID overwrites RAX as well as RBX
 *     xchg %rax, %rbx     ; so this restores RBX from CPUID's EAX output
 *
 * which both queried a garbage leaf and destroyed RBX -- a callee-saved
 * register. Any caller holding a live value there got it back corrupted;
 * ds_str_find kept its `src` argument in RBX and segfaulted on return.
 * Nothing about the C was wrong, which is exactly why hand-written CPUID
 * asm is worth avoiding when the compiler ships a correct version.
 */
int ds_cpu_has_avx2(void) {
  static int cached = -1; /* -1 = not probed yet */

  if (cached >= 0) return cached;
  cached = 0;

  if (getenv("DS_NO_AVX2") != NULL) return cached;

  __builtin_cpu_init();
  cached = __builtin_cpu_supports("avx2") ? 1 : 0;
  return cached;
}

#endif /* DS_COMPILER_MSVC */

#else /* non-x86: ARM, RISC-V, ... */

int ds_cpu_has_avx2(void) { return 0; }

#endif /* DS_ARCH_X86 */
