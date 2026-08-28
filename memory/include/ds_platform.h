#ifndef DS_PLATFORM_H
#define DS_PLATFORM_H

/*
 * ds_platform.h -- compiler / OS / architecture abstraction layer.
 *
 * Everything in the framework that is not plain ISO C11 goes through this
 * header, so that the rest of the sources stay compiler-neutral.
 *
 * Supported toolchains: GCC, Clang, MSVC (>= 19.28 / VS 2019 16.8 for C11),
 * MinGW-w64. Supported targets: Windows, Linux, macOS, any POSIX.2001 system.
 */

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Compiler identification                                             */
/* ------------------------------------------------------------------ */

#if defined(_MSC_VER) && !defined(__clang__)
#define DS_COMPILER_MSVC 1
#else
#define DS_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#define DS_COMPILER_CLANG 1
#else
#define DS_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define DS_COMPILER_GCC 1
#else
#define DS_COMPILER_GCC 0
#endif

/* ------------------------------------------------------------------ */
/* Platform identification                                             */
/* ------------------------------------------------------------------ */

#if defined(_WIN32) || defined(_WIN64)
#define DS_OS_WINDOWS 1
#else
#define DS_OS_WINDOWS 0
#endif

#if defined(__APPLE__)
#define DS_OS_MACOS 1
#else
#define DS_OS_MACOS 0
#endif

/* ------------------------------------------------------------------ */
/* Architecture identification                                         */
/* ------------------------------------------------------------------ */

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define DS_ARCH_X86 1
#else
#define DS_ARCH_X86 0
#endif

/* ------------------------------------------------------------------ */
/* Language / attribute shims                                          */
/* ------------------------------------------------------------------ */

/*
 * MSVC only grew a working <stdalign.h> in the C11 mode of VS 2019 16.8.
 * Older MSVC (and MSVC in C89/C99 mode) needs __declspec(align()).
 */
#if DS_COMPILER_MSVC
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && _MSC_VER >= 1928
#include <stdalign.h>
#define DS_ALIGNAS(n) alignas(n)
#else
#define DS_ALIGNAS(n) __declspec(align(n))
#endif
#else
#include <stdalign.h>
#define DS_ALIGNAS(n) alignas(n)
#endif

#if DS_COMPILER_MSVC
#define DS_INLINE __inline
#define DS_FORCE_INLINE __forceinline
#define DS_UNUSED(x) (void)(x)
#else
#define DS_INLINE inline
#define DS_FORCE_INLINE inline __attribute__((always_inline))
#define DS_UNUSED(x) (void)(x)
#endif

/* MSVC's CRT flags snprintf/vsnprintf/strcpy as "unsafe"; we use them
 * correctly and portably, so silence the deprecation wholesale. */
#if DS_COMPILER_MSVC && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1
#endif

/* ------------------------------------------------------------------ */
/* Count-trailing-zeros (portable)                                     */
/* ------------------------------------------------------------------ */

#if DS_COMPILER_MSVC
#include <intrin.h>
static DS_INLINE unsigned ds_ctz32(uint32_t x) {
  unsigned long idx;
  if (x == 0u) return 32u;
  _BitScanForward(&idx, (unsigned long)x);
  return (unsigned)idx;
}
#else
static DS_INLINE unsigned ds_ctz32(uint32_t x) {
  if (x == 0u) return 32u;
  return (unsigned)__builtin_ctz(x);
}
#endif

/* ------------------------------------------------------------------ */
/* SIMD availability                                                   */
/* ------------------------------------------------------------------ */

/*
 * DS_HAVE_AVX2_INTRINSICS says only "the compiler can emit AVX2 intrinsics".
 * Whether the *running* CPU supports them is a separate, runtime question
 * answered by ds_cpu_has_avx2(). Never gate execution on the macro alone.
 *
 * MSVC always exposes the AVX2 intrinsics (it has no -mavx2 requirement);
 * GCC/Clang need either -mavx2 globally or a target attribute on the
 * function, so we compile the AVX2 kernel with a function-level target
 * attribute and dispatch to it at runtime.
 */
#if DS_ARCH_X86
#if DS_COMPILER_MSVC
#define DS_HAVE_AVX2_INTRINSICS 1
#define DS_TARGET_AVX2
#elif DS_COMPILER_GCC || DS_COMPILER_CLANG
#define DS_HAVE_AVX2_INTRINSICS 1
#define DS_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define DS_HAVE_AVX2_INTRINSICS 0
#define DS_TARGET_AVX2
#endif
#else
#define DS_HAVE_AVX2_INTRINSICS 0
#define DS_TARGET_AVX2
#endif

/* Runtime CPU feature probe. Returns 1 if AVX2 is usable (CPU supports it
 * AND the OS saves the YMM register state), 0 otherwise. Always returns 0
 * on non-x86 targets. The result is computed once and cached. */
int ds_cpu_has_avx2(void);

/* ------------------------------------------------------------------ */
/* Monotonic wall-clock                                                */
/* ------------------------------------------------------------------ */

/*
 * Milliseconds from an unspecified but monotonic origin. Backed by
 * QueryPerformanceCounter on Windows, clock_gettime(CLOCK_MONOTONIC) on
 * POSIX, and mach_absolute_time on older macOS. Only differences between
 * two readings are meaningful.
 *
 * This replaces the direct clock_gettime() calls in the benchmarks, which
 * do not exist in the MSVC CRT.
 */
double ds_time_ms(void);

#endif /* DS_PLATFORM_H */
