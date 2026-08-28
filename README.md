# Managed Arena & Tagged-Pointer Data Structures Framework

A C11 data structures library backed by a **managed memory arena** and a
**mark-and-sweep garbage collector**.

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

### 2. Arena with a chunk-local free list

Rather than blocking memory until the arena is destroyed, dead blocks are
pushed onto a free list that is confined to the chunk they came from, so a
block can never be handed back into a different chunk's address range.
Allocation checks that free list (up to `DS_FREELIST_SCAN_LIMIT` cells, which
keeps allocation O(1) rather than O(free blocks)) before advancing the bump
pointer, and adjacent blocks are coalesced on release.

### 3. Descriptor-driven mark and sweep

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

### 4. Runtime-dispatched SIMD

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

### Build options

| Option | Default | Effect |
| --- | --- | --- |
| `DS_BUILD_TESTS` | `ON` | Build and register the `ds_tests` suite |
| `DS_BUILD_BENCH` | `ON` | Build the benchmark executables |
| `DS_WERROR` | `OFF` | Treat compiler warnings as errors |
| `DS_NATIVE_ARCH` | `OFF` | Add `-march=native`. Faster, but the resulting binary is not portable to other CPUs — leave it off for anything you distribute |

---

## API Example

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

| Header | Contents |
| --- | --- |
| `memory/include/ds_platform.h` | Compiler/OS/arch shims, `ds_time_ms`, `ds_cpu_has_avx2` |
| `memory/include/common.h` | `ds_node_t`, tagging helpers, arena and GC types |
| `memory/include/ds_arena.h` | Arena allocation and recycling |
| `memory/include/gc.h` | Roots, registration, `ds_arena_run_gc` |
| `ds_dyn_array.h` | `ds_array_t` (struct handle) and `ds_da_*` (bare pointer) arrays |
| `ds_string.h` | Length-prefixed strings, SIMD substring search, split/join/format |
| `ds_linked_list.h` | Circular doubly linked list |
| `ds_stack_queue.h` | Stack and queue over the list |
| `ds_priority_queue.h` | Binary min-heap |
| `ds_hash_map.h` | String-keyed chaining hash map with auto-resize |
| `ds_graph.h` | Adjacency-list graph |

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
- `ds_arena_alloc_raw(a, size)` — **unmanaged**. Never swept, never traced. Use
  it for payload buffers owned by a managed object, and release them from that
  object's `finalize` callback.
- Do not call `ds_arena_recycle` on a managed object that is still reachable;
  let the collector handle it.
- A GC root points at a `ds_node_t` *variable*. If that variable is a local,
  call `ds_gc_unregister_root` before the frame goes away.

## License

MIT. See [LICENSE](LICENSE).
