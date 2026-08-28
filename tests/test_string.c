/* Length-prefixed strings, SIMD substring search, split/join/replace. */

#include <string.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "ds_platform.h"
#include "ds_string.h"
#include "ds_test.h"

static void test_construction(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_string_t *s;

  SECTION("construction");

  s = ds_str_new(a, "hello world");
  CHECK(s && s->length == 11, "length is recorded");
  CHECK(strcmp(s->data, "hello world") == 0, "bytes are copied");
  CHECK(s->data[s->length] == '\0', "the buffer stays NUL-terminated");

  CHECK(ds_str_new(a, NULL) == NULL, "a NULL C string yields NULL");
  CHECK(ds_str_new(a, "")->length == 0, "the empty string is representable");

  s = ds_str_new_len(a, "abcdef", 3);
  CHECK(s->length == 3 && strcmp(s->data, "abc") == 0, "an explicit length truncates");

  s = ds_str_dup(a, ds_str_new(a, "copy me"));
  CHECK(strcmp(s->data, "copy me") == 0, "dup copies");
  CHECK(ds_str_dup(a, NULL) == NULL, "dup of NULL is NULL");

  /* Embedded NULs are legal: length is authoritative, not strlen. */
  s = ds_str_new_len(a, "a\0b", 3);
  CHECK(s->length == 3 && s->data[1] == '\0' && s->data[2] == 'b', "embedded NUL bytes survive");

  ds_arena_destroy(a);
}

static void test_compare(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_string_t *s = ds_str_new(a, "abc");

  SECTION("comparison");

  CHECK(ds_str_equal(s, ds_str_new(a, "abc")), "equal strings compare equal");
  CHECK(!ds_str_equal(s, ds_str_new(a, "abd")), "different strings do not");
  CHECK(!ds_str_equal(s, ds_str_new(a, "ab")), "different lengths do not");
  CHECK(ds_str_equal(s, s), "a string equals itself");
  CHECK(!ds_str_equal(s, NULL), "nothing equals NULL");
  CHECK(ds_str_equal(NULL, NULL), "NULL equals NULL");

  CHECK(ds_str_compare(s, ds_str_new(a, "abd")) < 0, "compare orders lexicographically");
  CHECK(ds_str_compare(s, ds_str_new(a, "ab")) > 0, "a prefix sorts first");
  CHECK(ds_str_compare(s, ds_str_new(a, "abc")) == 0, "equal strings compare zero");
  CHECK(ds_str_compare(NULL, s) < 0 && ds_str_compare(s, NULL) > 0, "NULL sorts first");

  ds_arena_destroy(a);
}

static void test_find(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_string_t *h;

  SECTION("substring search");

  h = ds_str_new(a, "hello world");
  CHECK(ds_str_find(h, ds_str_new(a, "world")) == 6, "finds a substring");
  CHECK(ds_str_find(h, ds_str_new(a, "hello")) == 0, "finds a prefix");
  CHECK(ds_str_find(h, ds_str_new(a, "zebra")) == -1, "reports a miss");
  CHECK(ds_str_find(h, ds_str_new(a, "")) == 0, "the empty needle matches at zero");
  CHECK(ds_str_find(h, ds_str_new(a, "hello world!")) == -1, "an over-long needle misses");
  CHECK(ds_str_find(NULL, h) == -1 && ds_str_find(h, NULL) == -1, "NULL arguments miss");

  /* The old scalar tail ran a fixed 33 iterations with
   * memcmp(h + i, n + i, n_len), so it read past short buffers and
   * compared the needle against itself. */
  CHECK(ds_str_find(ds_str_new(a, "abcdefghij"), ds_str_new(a, "hij")) == 7,
        "regression: match at the very end of a short string");

  CHECK(ds_str_find(ds_str_new(a, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab"), ds_str_new(a, "ab")) == 47,
        "regression: match straddling the AVX2 tail boundary");

  {
    char buf[300];
    memset(buf, 'x', sizeof(buf));
    memcpy(buf + 280, "NEEDLE", 6);
    CHECK(ds_str_find(ds_str_new_len(a, buf, sizeof(buf)), ds_str_new(a, "NEEDLE")) == 280,
          "regression: match in the last AVX2 block");
  }

  ds_arena_destroy(a);
}

static void test_find_against_strstr(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  char buf[160];
  int len, off, mismatches = 0;

  SECTION("search cross-checked with strstr");

  /* Exhaustive agreement with the C library across every length and offset,
   * so both the vector body and the scalar tail are covered and the
   * expectation cannot drift from the data. */
  for (len = 1; len <= 150; len += 3) {
    for (off = 0; off + 3 <= len; off += 2) {
      const char *ref;
      ptrdiff_t got;
      memset(buf, 'z', (size_t)len);
      buf[len] = '\0';
      memcpy(buf + off, "qXq", 3);
      ref = strstr(buf, "qXq");
      got = ds_str_find(ds_str_new_len(a, buf, (size_t)len), ds_str_new(a, "qXq"));
      if (!ref || got != (ptrdiff_t)(ref - buf)) mismatches++;
    }
  }
  CHECK(mismatches == 0, "find agrees with strstr across every length and offset");

  /* A needle whose first byte recurs constantly exercises the candidate
   * loop over the comparison mask. */
  {
    const char *ref;
    memset(buf, 'a', 150);
    buf[150] = '\0';
    buf[149] = 'b';
    ref = strstr(buf, "aab");
    CHECK(ref != NULL && ds_str_find(ds_str_new(a, buf), ds_str_new(a, "aab")) == (ptrdiff_t)(ref - buf),
          "many false candidates before the real match");
  }

  printf("        (AVX2 path %s for this run)\n", ds_cpu_has_avx2() ? "active" : "disabled");

  ds_arena_destroy(a);
}

static void test_transforms(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_string_t *s, *r;

  SECTION("transforms");

  r = ds_str_concat(a, ds_str_new(a, "one"), ds_str_new(a, "two"));
  CHECK(r->length == 6 && strcmp(r->data, "onetwo") == 0, "concat joins");
  CHECK(strcmp(ds_str_concat(a, NULL, ds_str_new(a, "x"))->data, "x") == 0, "concat tolerates NULL");

  r = ds_str_substr(a, ds_str_new(a, "abcdef"), 2, 3);
  CHECK(strcmp(r->data, "cde") == 0, "substr slices");
  CHECK(ds_str_substr(a, ds_str_new(a, "abc"), 1, 99)->length == 2, "substr clamps an over-long span");
  CHECK(ds_str_substr(a, ds_str_new(a, "abc"), 9, 1)->length == 0, "substr past the end is empty");

  r = ds_str_trim(a, ds_str_new(a, "  padded \t\n"));
  CHECK(strcmp(r->data, "padded") == 0, "trim strips both ends");
  CHECK(ds_str_trim(a, ds_str_new(a, "   "))->length == 0, "an all-space string trims to empty");

  s = ds_str_new(a, "abcabcabc");
  r = ds_str_replace(a, s, ds_str_new(a, "abc"), ds_str_new(a, "X"));
  CHECK(strcmp(r->data, "XXX") == 0 && r->length == 3, "replace shrinks");
  r = ds_str_replace(a, s, ds_str_new(a, "b"), ds_str_new(a, "LONG"));
  CHECK(strcmp(r->data, "aLONGcaLONGcaLONGc") == 0, "replace grows");
  r = ds_str_replace(a, s, ds_str_new(a, "zzz"), ds_str_new(a, "X"));
  CHECK(strcmp(r->data, "abcabcabc") == 0, "replacing an absent needle copies the source");
  r = ds_str_replace(a, s, ds_str_new(a, "abc"), NULL);
  CHECK(r->length == 0, "a NULL replacement deletes");

  r = ds_str_format(a, "%s=%d/%.1f", "k", 7, 2.5);
  CHECK(strcmp(r->data, "k=7/2.5") == 0, "format renders");
  CHECK(ds_str_format(a, "")->length == 0, "an empty format yields an empty string");

  ds_arena_destroy(a);
}

static void test_split_join(void) {
  _ds_arena_t_ *a = ds_arena_new(0);
  ds_string_t **parts;

  SECTION("split and join");

  parts = ds_str_split(a, ds_str_new(a, "a,b,c"), ds_str_new(a, ","));
  CHECK(ds_da_len(parts) == 3, "split produces three fields");
  if (ds_da_len(parts) == 3) {
    CHECK(strcmp(parts[0]->data, "a") == 0, "field 0");
    CHECK(strcmp(parts[1]->data, "b") == 0, "field 1");
    CHECK(strcmp(parts[2]->data, "c") == 0, "field 2");
  }
  CHECK(strcmp(ds_str_join(a, parts, ds_str_new(a, "-"))->data, "a-b-c") == 0, "join round-trips the split");

  parts = ds_str_split(a, ds_str_new(a, "a,"), ds_str_new(a, ","));
  CHECK(ds_da_len(parts) == 2 && parts[1]->length == 0,
        "regression: a trailing delimiter yields a trailing empty field");

  parts = ds_str_split(a, ds_str_new(a, ",a"), ds_str_new(a, ","));
  CHECK(ds_da_len(parts) == 2 && parts[0]->length == 0, "a leading delimiter yields a leading empty field");

  parts = ds_str_split(a, ds_str_new(a, "abc"), ds_str_new(a, "::"));
  CHECK(ds_da_len(parts) == 1 && strcmp(parts[0]->data, "abc") == 0,
        "a delimiter longer than the input yields the whole input");

  parts = ds_str_split(a, ds_str_new(a, "a--b"), ds_str_new(a, "--"));
  CHECK(ds_da_len(parts) == 2 && strcmp(parts[1]->data, "b") == 0, "multi-character delimiters work");

  CHECK(ds_str_join(a, NULL, ds_str_new(a, ","))->length == 0, "joining nothing yields the empty string");

  ds_arena_destroy(a);
}

int main(void) {
  ds_test_begin("string");
  test_construction();
  test_compare();
  test_find();
  test_find_against_strstr();
  test_transforms();
  test_split_join();
  return ds_test_end();
}
