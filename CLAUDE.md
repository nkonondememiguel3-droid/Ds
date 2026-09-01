# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release      # Release is forced if no build type is given
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Single test suite (each structure is its own executable and its own ctest entry):

```bash
./build/test_hash_map                          # run directly, prints per-check failures with file:line
ctest --test-dir build -R hash_map --output-on-failure
```

Static libraries land in `build/libds.a` and `build/memory/libds_gc.a`, `build/memory/libds_arena.a`.

ctest names are the bare suite names: `common arena gc dyn_array string linked_list stack_queue
priority_queue hash_map graph`, plus `fuzz_string`, `fuzz_arena`, `fuzz_containers`.

Benchmarks and fuzzers:

```bash
cmake --build build --target bench             # builds and runs all nine bench_* programs
./build/bench_dyn_array                        # or run one
./build/fuzz_string 5000000 12345              # <iterations> <seed>; the seed is always printed for replay
```

The fuzzers run a short 3000-iteration pass as part of `ctest`. `-DDS_LIBFUZZER=ON` (clang only)
swaps the built-in deterministic driver for coverage-guided libFuzzer.

Useful options: `-DDS_WERROR=ON`, `-DDS_NATIVE_ARCH=ON` (adds `-march=native`, non-portable),
`-DDS_BUILD_TESTS/FUZZ/BENCH=OFF`. Install/uninstall: `sudo cmake --install build` /
`sudo cmake --build build --target uninstall` (the uninstall target reads `build/install_manifest.txt`,
so deleting the build tree loses the ability to uninstall).

Formatting is `.clang-format` (Google base, 2-space indent, 120 columns, left-aligned pointers).

## Architecture

Three static libraries, in a strict dependency order — `libds.a` (repo root, the data structures)
→ `libds_gc.a` (`memory/gc.c`, mark and sweep) → `libds_arena.a` (`memory/ds_arena.c`, the allocator).
Consumers link `-lds -lds_gc -lds_arena` in that order, or any suffix of it. Headers include each
other by bare name and install flattened into one directory.

**The arena must stay usable on its own.** No file in `libds_arena.a` may include `gc.h`; the arena
struct holds no collector state, only an opaque `collector` pointer and a `collector_destroy` hook
that `ds_arena_destroy` calls. `gc.c` attaches a `ds_gc_t` (roots + mark stack) there on first use.
`test_arena` and `bench_arena` link `ds_arena` alone — listed in `DS_ARENA_ONLY` in the root
`CMakeLists.txt` — so a new arena→collector dependency breaks their link rather than passing
unnoticed.

### Tagged 64-bit words

`ds_node_t` (`memory/include/ds_value.h`) is a fixed `uint64_t`, deliberately **not** `uintptr_t` — the
payload of an `int`/`float` is shifted left by 4 to make room for the tag, which would truncate on
32-bit targets. Every value in the library is one of these words: `TYPE_INT/FLOAT/BOOL/NIL` are
encoded inline with no allocation, `TYPE_STRING/TYPE_NODE` carry a heap pointer whose low 4 bits are
guaranteed zero by the arena's 16-byte alignment. Use `ds_make_*`/`ds_unpack_*`/`ds_tag_ptr`/`ds_get_ptr`;
never dereference a `ds_node_t` without masking. `ds_value.h` is header-only and allocator-free — the
arena never includes it, since a byte allocator has no opinion about what is stored in the bytes. The
static assert tying `ARENA_ALIGN` to `DS_TAG_BITS` therefore lives in `gc.h`, the layer that needs both.
`common.h` is now only a back-compat umbrella over `ds_value.h` + `ds_arena.h` + `gc.h`; new arena-only
code should include `ds_arena.h` directly.

### Block headers, not a side table

Every byte of a chunk's used region is covered by exactly one `_ds_block_header_t_` immediately
preceding the payload. That header is exactly `ARENA_ALIGN` (16) bytes — there is a static assert on
this, and a 32-bit-only `reserved_` pad to keep it true on ILP32. Because the header is adjacent,
marking is a pointer subtraction (`ds_block_of`) and sweeping is a linear address-order walk per chunk,
which also makes coalescing of adjacent dead blocks fall out for free. Do not reintroduce a pointer→object
hash table.

Free blocks are threaded onto a **chunk-local** free list, so a block can never be handed back into a
different chunk's address range; chunks with free space are also threaded onto a second `free_chunks`
ring. Allocation walks at most `DS_FREELIST_SCAN_LIMIT` (32) cells before falling back to the bump
pointer, which keeps allocation O(1) instead of O(free blocks).

The header's `descriptor` field is opaque to the arena — declared there as an incomplete type, stored
and returned, never dereferenced. `ds_arena_block_promote`/`_demote`/`_retire` are the arena-side
state transitions a collector drives (they keep `live_managed_*` correct), and
`ds_arena_rebuild_free_lists` derives every free list from block state alone, coalescing runs of
adjacent free blocks. That last one is why the sweep's two phases work: phase A (`gc.c`) finalizes and
retires without touching a list, phase B (arena) rebuilds from scratch, so a block a finalizer
recycled mid-sweep is discovered exactly once instead of being linked twice into a cyclic list.

### Descriptor-driven mark and sweep

`memory/gc.c` knows nothing about lists, maps or graphs — nor, going the other way, does the arena
know anything about it. Each type defines a `ds_type_descriptor_t { mark, finalize }` at file scope in its own `.c` (see `ds_list_descriptor`,
`ds_map_descriptor`, `ds_graph_descriptor`, …) and passes it to `ds_arena_alloc`. `mark` calls
`ds_gc_push_mark_stack_context` for each referenced node — marking is iterative and cycle-safe.
`finalize` releases raw buffers the object owns. **Adding a new container type means adding a
descriptor**; a container allocated with a NULL descriptor that holds references will have its
children collected out from under it (this was a real bug in `ds_graph.c`).

Allocation kinds, and the ownership rule that follows from them:

- `ds_arena_alloc(a, size, desc)` — managed, traced, swept. For objects.
- `ds_arena_alloc_raw(a, size)` — unmanaged, never traced. For payload buffers owned by a managed
  object; release them from that object's `finalize`.
- Never `ds_arena_recycle` a managed object that is still reachable.
- A GC root registered with `ds_gc_register_root` points at a `ds_node_t` *variable*; unregister a
  local before its frame dies.

### Portability shims

`memory/include/ds_platform.h` centralises compiler/OS/arch differences (`DS_INLINE`, `DS_ALIGNAS`,
`ds_time_ms`, `ds_cpu_has_avx2`). The build targets GCC, Clang, MSVC and MinGW-w64 on Linux/macOS/Windows,
32- and 64-bit; CI builds all three OSes. Compiler flags are set per target through `ds_set_target_flags()`
in the root `CMakeLists.txt` rather than with global `add_compile_options`, because the GCC and MSVC
spellings differ — keep new flags inside that function.

AVX2 is runtime-dispatched: `ds_str_find` uses the AVX2 kernel only when the compiler could emit it
*and* `ds_cpu_has_avx2()` (which includes the `XGETBV` YMM check) agrees at run time. `DS_NO_AVX2=1` in
the environment forces the scalar path — useful for differential testing. Never gate execution on the
compile-time macro alone.

## Repo layout notes

- Structures live as flat `ds_*.c` / `ds_*.h` pairs in the repo root; `memory/` is the only
  subdirectory, and it holds two libraries rather than one.
- `benchmark.c`, `benchmarck.c`, `dynamic_array_benchmark.c`, `tests/test_ds.c` and
  `tests/bench_dyn_array_performance.c` are **not in any CMake target** — they are superseded leftovers
  from before the per-structure split. Don't extend them; add to `tests/test_<suite>.c` or
  `tests/bench_<suite>.c` and to the `DS_TEST_SUITES`/`DS_BENCH_SUITES` lists instead.
- `examples/` is gitignored, and the README refers to a file (`examples/arena_frames.c`) that isn't
  the one on disk (`examples/arena-test.c`).
- The test/bench/fuzz harnesses (`tests/ds_test.h`, `ds_bench.h`, `ds_fuzz.h`) are header-only; there
  is nothing to link. Benchmarks report a median over `DS_BENCH_REPS` (7) repetitions with the observed
  range, not a single timing. Fuzzers check invariants against a reference model, not just absence of
  crashes.
