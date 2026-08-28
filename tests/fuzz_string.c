/*
 * String fuzzer.
 *
 * Every operation is checked against an independent reference written the
 * dumbest way possible, so a wrong answer fails as loudly as a bad pointer.
 * The references handle embedded NUL bytes, because ds_string_t is
 * length-prefixed and the library is expected to as well.
 */

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_fuzz.h"
#include "ds_string.h"
#include "gc.h"

const char *ds_fuzz_name = "fuzz_string";

#define MAX_STR 512

/* ------------------------------------------------------------------ */
/* References                                                          */
/* ------------------------------------------------------------------ */

/* Naive substring search over explicit lengths. */
static ptrdiff_t ref_find(const char *h, size_t hl, const char *n, size_t nl) {
  size_t i;
  if (nl == 0) return 0;
  if (nl > hl) return -1;
  for (i = 0; i + nl <= hl; i++) {
    if (memcmp(h + i, n, nl) == 0) return (ptrdiff_t)i;
  }
  return -1;
}

/* Left-to-right, non-overlapping replacement -- the same rule ds_str_replace
 * follows. Returns the output length, or (size_t)-1 if it would not fit. */
static size_t ref_replace(char *out, size_t cap, const char *s, size_t sl, const char *o, size_t ol, const char *n,
                          size_t nl) {
  size_t i = 0, w = 0;
  if (ol == 0) {
    if (sl > cap) return (size_t)-1;
    memcpy(out, s, sl);
    return sl;
  }
  while (i < sl) {
    if (i + ol <= sl && memcmp(s + i, o, ol) == 0) {
      if (w + nl > cap) return (size_t)-1;
      memcpy(out + w, n, nl);
      w += nl;
      i += ol;
    } else {
      if (w + 1 > cap) return (size_t)-1;
      out[w++] = s[i++];
    }
  }
  return w;
}

static size_t ref_count(const char *h, size_t hl, const char *n, size_t nl) {
  size_t count = 0, off = 0;
  if (nl == 0 || nl > hl) return 0;
  while (off + nl <= hl) {
    if (memcmp(h + off, n, nl) == 0) {
      count++;
      off += nl; /* non-overlapping, matching ds_str_replace */
    } else {
      off++;
    }
  }
  return count;
}

/* ------------------------------------------------------------------ */

static void check_find(_ds_arena_t_ *a, const char *hb, size_t hl, const char *nb, size_t nl) {
  ds_string_t *h = ds_str_new_len(a, hb, hl);
  ds_string_t *n = ds_str_new_len(a, nb, nl);
  ptrdiff_t got = ds_str_find(h, n);
  ptrdiff_t want = ref_find(hb, hl, nb, nl);

  FZ_CHECK(got == want, "ds_str_find disagrees with the naive reference");
  if (got >= 0) {
    FZ_CHECK((size_t)got + nl <= hl, "ds_str_find returned a match that runs past the haystack");
    FZ_CHECK(memcmp(h->data + got, nb, nl) == 0, "ds_str_find pointed at bytes that are not the needle");
  }
}

static void check_replace(_ds_arena_t_ *a, const char *sb, size_t sl, const char *ob, size_t ol, const char *nb,
                          size_t nl) {
  ds_string_t *src = ds_str_new_len(a, sb, sl);
  ds_string_t *old_sub = ds_str_new_len(a, ob, ol);
  ds_string_t *new_sub = ds_str_new_len(a, nb, nl);
  ds_string_t *res = ds_str_replace(a, src, old_sub, new_sub);
  size_t occurrences;

  if (!res) return; /* rejected the request; nothing to verify */

  if (ol == 0) {
    FZ_CHECK(res->length == sl, "replacing an empty needle changed the length");
    return;
  }

  /* The output length has to follow from the occurrence count exactly. */
  occurrences = ref_count(sb, sl, ob, ol);
  FZ_CHECK(res->length == sl + occurrences * nl - occurrences * ol,
           "ds_str_replace produced a length inconsistent with the occurrence count");
  FZ_CHECK(res->data[res->length] == '\0', "ds_str_replace left the result unterminated");

  /* Compare byte for byte against the reference.
   *
   * The obvious-looking invariant "no occurrence of the needle remains" is
   * WRONG, and this fuzzer asserted it until it produced a false report:
   * removing a match joins the text on either side of it, and that seam can
   * form a fresh occurrence that was never in the input. Replacing "ab"
   * with "" in "aabb" yields "ab". Comparing against a reference
   * implementation is both stricter and actually true. */
  {
    static char expect[32768];
    size_t want = ref_replace(expect, sizeof(expect), sb, sl, ob, ol, nb, nl);
    if (want != (size_t)-1) {
      FZ_CHECK(res->length == want, "ds_str_replace produced the wrong length");
      FZ_CHECK(memcmp(res->data, expect, want) == 0, "ds_str_replace produced the wrong bytes");
    }
  }
}

static void check_split_join(_ds_arena_t_ *a, const char *sb, size_t sl, const char *db, size_t dl) {
  ds_string_t *s = ds_str_new_len(a, sb, sl);
  ds_string_t *delim = ds_str_new_len(a, db, dl);
  ds_string_t **parts;
  ds_string_t *rejoined;
  size_t i, n;

  if (dl == 0 || dl > sl) return; /* documented as returning the whole input */

  parts = ds_str_split(a, s, delim);
  n = ds_da_len(parts);
  FZ_CHECK(n > 0, "ds_str_split returned no fields at all");

  for (i = 0; i < n; i++) FZ_CHECK(parts[i] != NULL, "ds_str_split produced a NULL field");

  /* Splitting on a delimiter and joining on the same one is the identity,
   * which pins down field boundaries far more tightly than any single
   * hand-written case could. */
  rejoined = ds_str_join(a, parts, delim);
  FZ_CHECK(rejoined != NULL, "ds_str_join returned NULL for a valid field list");
  FZ_CHECK(rejoined->length == s->length && memcmp(rejoined->data, s->data, s->length) == 0,
           "split followed by join did not reproduce the original string");
}

static void check_misc(_ds_arena_t_ *a, const char *sb, size_t sl, ds_fuzz_t *f) {
  ds_string_t *s = ds_str_new_len(a, sb, sl);
  size_t start = fz_range(f, 0, sl + 4);
  size_t len = fz_range(f, 0, sl + 4);
  ds_string_t *sub = ds_str_substr(a, s, start, len);
  ds_string_t *trimmed = ds_str_trim(a, s);
  ds_string_t *dup = ds_str_dup(a, s);
  ds_string_t *cat = ds_str_concat(a, s, s);

  FZ_CHECK(s->length == sl, "ds_str_new_len did not preserve the length");
  FZ_CHECK(s->data[sl] == '\0', "ds_str_new_len left the buffer unterminated");

  FZ_CHECK(sub->length <= sl, "ds_str_substr returned more bytes than the source holds");
  if (start < sl) {
    size_t want = (len > sl - start) ? sl - start : len;
    FZ_CHECK(sub->length == want, "ds_str_substr clamped the span incorrectly");
    FZ_CHECK(memcmp(sub->data, sb + start, want) == 0, "ds_str_substr returned the wrong bytes");
  } else {
    FZ_CHECK(sub->length == 0, "ds_str_substr past the end was not empty");
  }

  FZ_CHECK(trimmed->length <= sl, "ds_str_trim grew the string");

  FZ_CHECK(dup->length == sl && memcmp(dup->data, sb, sl) == 0, "ds_str_dup is not a faithful copy");
  FZ_CHECK(ds_str_equal(dup, s), "a duplicate does not compare equal to its source");
  FZ_CHECK(ds_str_compare(dup, s) == 0, "a duplicate does not compare zero against its source");

  FZ_CHECK(cat->length == 2 * sl, "ds_str_concat produced the wrong length");
  if (sl) {
    FZ_CHECK(memcmp(cat->data, sb, sl) == 0 && memcmp(cat->data + sl, sb, sl) == 0,
             "ds_str_concat produced the wrong bytes");
  }
}

/*
 * ds_str_format takes a programmer-supplied format string, so fuzzing the
 * format itself would only exercise the C library. What is worth fuzzing is
 * the argument that decides which side of the 256-byte stack buffer the
 * result lands on.
 */
static void check_format(_ds_arena_t_ *a, ds_fuzz_t *f) {
  static char pattern[1024];
  size_t n = fz_range(f, 0, sizeof(pattern) - 1);
  ds_string_t *res;

  memset(pattern, 'F', sizeof(pattern));
  res = ds_str_format(a, "%.*s", (int)n, pattern);

  FZ_CHECK(res != NULL, "ds_str_format returned NULL for a valid format");
  FZ_CHECK(res->length == n, "ds_str_format produced the wrong length");
  FZ_CHECK(res->data[n] == '\0', "ds_str_format left the result unterminated");
  if (n) FZ_CHECK(memcmp(res->data, pattern, n) == 0, "ds_str_format produced the wrong bytes");

  res = ds_str_format(a, "%s-%d-%u", "k", (int)fz_u32(f), (unsigned)fz_u32(f));
  FZ_CHECK(res != NULL && res->data[res->length] == '\0', "ds_str_format mishandled a mixed format");
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static char a_buf[MAX_STR], b_buf[MAX_STR], c_buf[MAX_STR];
  ds_fuzz_t f;
  _ds_arena_t_ *arena;
  size_t al, bl, cl;
  int narrow;

  if (size < 8) return 0;

  fz_init(&f, data, size);
  arena = ds_arena_new(0);

  /* Half the time draw from a three-letter alphabet, so that matches,
   * repeated needles and overlapping candidates actually occur. */
  narrow = (fz_u8(&f) & 1) != 0;
  if (narrow) {
    al = fz_bytes_narrow(&f, a_buf, MAX_STR);
    bl = fz_bytes_narrow(&f, b_buf, 32);
    cl = fz_bytes_narrow(&f, c_buf, 32);
  } else {
    al = fz_bytes(&f, a_buf, MAX_STR);
    bl = fz_bytes(&f, b_buf, 32);
    cl = fz_bytes(&f, c_buf, 32);
  }

  check_find(arena, a_buf, al, b_buf, bl);
  check_find(arena, a_buf, al, a_buf, al); /* a string always contains itself */
  check_replace(arena, a_buf, al, b_buf, bl, c_buf, cl);
  check_split_join(arena, a_buf, al, b_buf, bl);
  check_misc(arena, a_buf, al, &f);
  check_format(arena, &f);

  /* A collection in the middle of all this must not disturb anything, and
   * a second one must be idempotent. */
  ds_arena_run_gc(arena);
  ds_arena_run_gc(arena);

  ds_arena_destroy(arena);
  return 0;
}
