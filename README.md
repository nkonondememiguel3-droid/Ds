# Managed Arena & Tagged-Pointer Data Structures Framework

A C11 data structures library backed by a **managed memory arena** and a
**mark-and-sweep garbage collector**.

The arena and the collector are **two separate libraries**. `libds_arena.a`
is a standalone allocator that knows nothing about values, roots or
collection; `libds_gc.a` is a collector layered on top of it. Link the arena
on its own and no collector code enters your program.

By leveraging **tagged pointers** on a strict 16-byte alignment constraint, the
framework avoids `malloc`/`free` on the hot path, keeps allocations dense for
cache locality, and tracks live objects without a per-object header.

Builds and passes its test suite on **Linux, macOS and Windows**, under
**GCC, Clang, MSVC and MinGW-w64**, on 32-bit and 64-bit targets.

## Key Architectural Ideas

### 1. 16-byte aligned tagged pointers

Memory returned by the arena is aligned to 16 bytes, which guarantees the four
least significant bits of any object address are zero. Those four bits carry a
type tag inside the pointer word itself:

- Integers, floats and booleans are encoded directly in the `ds_node_t` word
  and need no heap allocation at all.
- Heap objects (strings, list nodes, maps) clear the tag with `& ~0xF` before
  dereferencing.

`ds_node_t` is a fixed 64-bit word rather than a `uintptr_t`, so the encoding
holds a full 32-bit `int` or `float` payload plus its tag on 32-bit targets too.

### 2. Three layers, one dependency direction

```
 ds_value.h    the tagged word           (header only, no allocator)
 libds_arena.a the allocator             ds_arena.h
 libds_gc.a    mark and sweep            gc.h        -> depends on ds_arena
 libds.a       the data structures       ds_*.h      -> depends on ds_gc
```

The arrows never point upwards. No arena source includes `gc.h`, and the
arena struct holds no collector state: a collector attaches its own state to
the arena's opaque `collector` slot the first time something needs it, along
with the teardown hook `ds_arena_destroy` calls.

What the arena does provide is the substrate a collector needs -- a state, a
mark bit and one opaque metadata pointer in every block header, the ability
to move blocks between the traced and untraced states, and a free-list
rebuild that derives the lists from block state alone. The collector decides
which blocks are dead; the arena decides how free space is tracked. Anyone
can write a different policy on the same hooks.

### 3. Arena with a chunk-local free list

Rather than blocking memory until the arena is destroyed, dead blocks are
pushed onto a free list that is confined to the chunk they came from, so a
block can never be handed back into a different chunk's address range.
Allocation checks that free list (up to `DS_FREELIST_SCAN_LIMIT` cells, which
keeps allocation O(1) rather than O(free blocks)) before advancing the bump
pointer, and adjacent blocks are coalesced on release.

### 4. Descriptor-driven mark and sweep

The collector is generic: it knows nothing about lists, maps or graphs. Each
type registers a `ds_type_descriptor_t` with a `mark` callback (enqueue the
objects I reference) and a `finalize` callback (release the raw buffers I own).
Marking is iterative and cycle-safe, so mutually referencing structures such as
`A <-> B` are collected correctly instead of leaking.

```c
static void my_type_mark(ds_node_t node, _ds_arena_t_ *a) {
    MyType *self = (MyType *)ds_get_ptr(node);
    ds_gc_push_mark_stack_context(a, self->some_child);
}

static void my_type_finalize(void *ptr, _ds_arena_t_ *a) {
    MyType *self = (MyType *)ptr;
    ds_arena_recycle_raw(a, self->raw_buffer, self->raw_buffer_bytes);
}

static const ds_type_descriptor_t my_descriptor = { my_type_mark, my_type_finalize };
```

### 5. Runtime-dispatched SIMD

`ds_str_find` uses an AVX2 kernel when the compiler can emit it *and*
`ds_cpu_has_avx2()` confirms the running CPU and OS support it (including the
`XGETBV` check for YMM state saving). Everywhere else — non-x86, older x86, a
build without AVX2 support — it falls back to a scalar `memchr`-driven search
that produces identical results. Set `DS_NO_AVX2=1` in the environment to force
the scalar path.

---

## Getting Started

### Prerequisites

- **CMake** 3.20 or newer
- A C11 compiler: GCC, Clang, MSVC (VS 2019 16.8+) or MinGW-w64

### Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

On Windows the same three commands work verbatim from a Developer Command
Prompt, or with `-G "Visual Studio 17 2022"`.

### Installing

```bash
sudo cmake --install build            # or: cd build && sudo make install
```

Headers land in `<prefix>/include` and the three static libraries
(`libds_arena.a`, `libds_gc.a`, `libds.a`) in `<prefix>/lib`, where `<prefix>` defaults to `/usr/local`. Pass
`--prefix <dir>` to install elsewhere.

To remove it again:

```bash
sudo cmake --build build --target uninstall   # or: cd build && sudo make uninstall
```

The uninstall target reads `install_manifest.txt` from the build directory,
so it deletes exactly what was installed. Keep the build directory around if
you want it -- deleting the build tree takes the manifest with it, and the
files then have to be removed by hand.

### Linking against it

The arena on its own -- a fast bump/free-list allocator, no collector, no
`ds_node_t`:

```bash
cc -std=c11 -O2 -o myprog myprog.c -lds_arena
```

The arena plus the collector:

```bash
cc -std=c11 -O2 -o myprog myprog.c -lds_gc -lds_arena
```

Any of the data structures as well:

```bash
cc -std=c11 -O2 -o myprog myprog.c -lds -lds_gc -lds_arena
```

Each library depends only on the ones to its right, so that is the order
they go in. `libds_arena.a` is self-contained.

### Build options

| Option | Default | Effect |
| --- | --- | --- |
| `DS_BUILD_TESTS` | `ON` | Build the ten per-structure test suites and register them with ctest |
| `DS_BUILD_BENCH` | `ON` | Build the nine benchmark programs; `--target bench` runs them all |
| `DS_BUILD_FUZZ` | `ON` | Build the three fuzz harnesses |
| `DS_LIBFUZZER` | `OFF` | Drive the fuzzers with clang's libFuzzer instead of the built-in deterministic driver |
| `DS_WERROR` | `OFF` | Treat compiler warnings as errors |
| `DS_NATIVE_ARCH` | `OFF` | Add `-march=native`. Faster, but the resulting binary is not portable to other CPUs — leave it off for anything you distribute |

---

## API Example

### Arena alone

No collector, no tagged words, one header and one library. Blocks are freed
explicitly or when the arena dies.

```c
#include <stdio.h>

#include "ds_arena.h"

int main(void) {
    _ds_arena_t_ *arena = ds_arena_new(0);   /* 0 = default 1 MiB chunks */
    char *frame[64];
    int i;

    for (i = 0; i < 64; i++) frame[i] = (char *)ds_arena_alloc_raw(arena, 100);
    for (i = 0; i < 64; i++) ds_arena_recycle_raw(arena, frame[i], 100);

    /* Recycling links blocks onto their chunk's free list one at a time.
     * This pass merges the adjacent ones back into large blocks -- the same
     * pass a collector's sweep ends with, available without one. */
    ds_arena_rebuild_free_lists(arena);

    printf("%zu bytes reusable\n", arena->total_free_bytes_in_list);

    ds_arena_destroy(arena);
    return 0;
}
```

```bash
cc -std=c11 -O2 -o frames frames.c -lds_arena
```

### Arena with the collector

```c
#include <stdio.h>

#include "ds_arena.h"
#include "ds_linked_list.h"
#include "gc.h"

int main(void) {
    /* 1. Create the arena (0 selects the default 1 MiB chunk size).
     *    ds_arena_new returns a POINTER; the arena is never a value type. */
    _ds_arena_t_ *arena = ds_arena_new(0);

    /* 2. Declare the entry point to your data as a tagged word. */
    ds_node_t my_list_root = ds_make_nil();

    /* 3. Register it as a GC root: anything reachable from it stays alive. */
    ds_gc_register_root(arena, &my_list_root);

    /* 4. Allocate from the arena, never from malloc. */
    ds_list_t *list = ds_list_new(arena);
    my_list_root = ds_tag_ptr(list, TYPE_NODE);

    ds_list_append(arena, list, ds_make_int(42));
    ds_list_append(arena, list, ds_make_int(100));
    ds_list_append(arena, list, ds_make_int(200));

    printf("Initial list size: %zu\n", list->length);   /* 3 */

    /* 5. Unlink a node; it goes straight onto the chunk's free list. */
    ds_list_node_t *doomed = ds_list_find(list, ds_make_int(100), NULL);
    if (doomed) ds_list_remove(arena, list, doomed);

    /* 6. Collect. The list and its remaining nodes are reachable from the
     *    root, so they survive; anything orphaned is swept. */
    ds_arena_run_gc(arena);

    /* 7. The next allocation reuses the recycled block in O(1). */
    ds_list_append(arena, list, ds_make_int(999));

    ds_arena_print_stats(arena);

    /* 8. Drop the root before its storage goes out of scope, then tear the
     *    arena down. ds_arena_destroy frees everything in one pass -- there
     *    is no separate collector teardown call. */
    ds_gc_unregister_root(arena, &my_list_root);
    ds_arena_destroy(arena);

    printf("Execution completed cleanly.\n");
    return 0;
}
```

---

## Modules

| Header | Library | Contents |
| --- | --- | --- |
| `memory/include/ds_platform.h` | -- | Compiler/OS/arch shims, `ds_time_ms`, `ds_cpu_has_avx2`, `DS_STATIC_ASSERT` |
| `memory/include/ds_value.h` | -- | `ds_node_t` and the tagging helpers. Header-only, depends on nothing |
| `memory/include/ds_arena.h` | `ds_arena` | Chunks, block headers, allocation, recycling, and the hooks a collector uses |
| `memory/include/gc.h` | `ds_gc` | Type descriptors, roots, `ds_gc_run` / `ds_arena_run_gc` |
| `memory/include/common.h` | -- | Compatibility umbrella over the three above. Pulls the collector in; new arena-only code should include `ds_arena.h` instead |
| `ds_dyn_array.h` | `ds` | `ds_array_t` (struct handle) and `ds_da_*` (bare pointer) arrays |
| `ds_string.h` | `ds` | Length-prefixed strings, SIMD substring search, split/join/format |
| `ds_linked_list.h` | `ds` | Circular doubly linked list |
| `ds_stack_queue.h` | `ds` | Stack and queue over the list |
| `ds_priority_queue.h` | `ds` | Binary min-heap |
| `ds_hash_map.h` | `ds` | String-keyed chaining hash map with auto-resize |
| `ds_graph.h` | `ds` | Adjacency-list graph |

### Two dynamic array styles

```c
/* Struct handle: explicit element size, works with any element type. */
ds_array_t *v = ds_array_new(arena, sizeof(int), 16, 0);
int x = 7;
ds_array_push(arena, v, &x);

/* Bare pointer: NULL is a valid empty array, no constructor needed. */
int *xs = NULL;
ds_da_push(arena, xs, 7);
printf("%zu of %zu\n", ds_da_len(xs), ds_da_cap(xs));
ds_da_free(arena, xs);
```

The `ds_da_*` macros evaluate their array argument more than once, so do not
pass an expression with side effects.

---

## Memory ownership rules

- `ds_arena_alloc(a, size, desc)` — **managed**. The collector may sweep it.
  Pass a descriptor if the object references other objects or owns raw buffers.
  With no collector linked in, a managed block behaves like a raw one that
  nothing ever frees; arena-only code should use `ds_arena_alloc_raw`.
- `ds_arena_alloc_raw(a, size)` — **unmanaged**. Never swept, never traced. Use
  it for payload buffers owned by a managed object, and release them from that
  object's `finalize` callback.
- Do not call `ds_arena_recycle` on a managed object that is still reachable;
  let the collector handle it.
- A GC root points at a `ds_node_t` *variable*. If that variable is a local,
  call `ds_gc_unregister_root` before the frame goes away.

## License

MIT. See [LICENSE](LICENSE).
