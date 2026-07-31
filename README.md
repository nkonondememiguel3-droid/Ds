# Managed Arena & Tagged-Pointer Data Structures Framework

A high-performance, low-latency C11 data structures library backed by a **Managed Memory Arena** and a **Mark-and-Sweep Garbage Collector**.

By leveraging **Tagged Pointers** on a strict 16-byte alignment constraint, this framework completely bypasses the OS kernel allocator (`malloc`/`free`) during runtime, achieving near-zero memory fragmentation, maximized CPU cache locality, and atomic-speed element tracking.

## Key Architectural Innovations

### 1. 16-Byte Aligned Tagged Pointers

On 64-bit systems, memory returned by our Arena is strictly aligned to 16 bytes. This guarantees that the **4 least significant bits** of any valid node address are always `0` (`...0000` in binary). We reclaim these 4 bits to store structural type identifiers (_Tags_) directly inside the pointer variable without dereferencing it:

- Immediate integers, floats, and booleans require **zero heap allocations**—their values are encoded directly inside the 64-bit `ds_node_t` pointer register.
- Complex data structures (strings, nodes, maps) clear the tag using a fast bitwise mask (`& ~0xF`) before reading the physical memory address.

### 2. Managed Arena with Segregated Free-List

Unlike standard bump allocators that block memory until the entire arena is destroyed, this library implements an explicit **Free-List recycling loop**.

- Dead nodes swept by the Garbage Collector are attached to an isolated, un-erasable metadata cell list.
- Future allocation requests intercept these cells in **O(1) time** instead of moving the global bump pointer forward, achieving an optimized memory footprint.

### 3. Decoupled Graph-Topology Garbage Collection

The framework includes a fully modular **Mark-and-Sweep Garbage Collector**. To prevent cyclic dependencies (such as directed graph loop mutations \(A \leftrightarrow B\)) from creating permanent memory leaks, the GC uses abstract connectivity tracing. Data structures hook into the GC using **callback extensions**, keeping the core memory manager completely generic.

---

## Getting Started

### Prerequisites

- **CMake** (v3.20 or higher)
- A C11 compatible compiler (**GCC**, **Clang**, or **MSVC**)

### Building the Project

We use CMake to compile both the memory static library (`libds_arena.a`), the primary data structures library (`libds.a`), and the main telemetry executable binary.

```bash
# Generate the build system directory
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile all modules (-O3 optimized)
cmake --build build --config Release
```

---

## API Code Example

The following example demonstrates how to initialize the **Managed Arena**, register a **Live Root Variable** to protect it from being swept, manipulate immediate data types via bitwise shifts, and trigger the internal **Garbage Collector** to recycle unreferenced blocks instantly.

```c
#include "ds_arena.h"
#include "ds_linked_list.h"
#include "gc.h"
#include <stdio.h>

int main() {
    // 1. Initialize the Managed Memory Arena (0 uses default 1MB chunks)
    _ds_arena_t_ arena = ds_arena_new(0);

    // 2. Declare your data structure entry point as a Tagged Pointer (ds_node_t)
    // Initially, set it to the fixed immediate NIL tag pattern
    ds_node_t my_list_root = ds_tag_ptr(NULL, TYPE_NIL);

    // 3. Register your root variable pointer with the Garbage Collector.
    // This variable acts as a 'Live Root'—anything reachable from it will be safe.
    ds_gc_register_root(&arena, &my_list_root);

    // 4. Allocate elements directly from the Arena without malloc()
    // Append an immediate 32-bit integer (Stores directly in the pointer register!)
    ds_list_t *list_handle = ds_list_new(&arena);
    my_list_root = ds_tag_ptr(list_handle, TYPE_NODE);

    ds_list_append(&arena, list_handle, ds_make_int(42));
    ds_list_append(&arena, list_handle, ds_make_int(100));
    ds_list_append(&arena, list_handle, ds_make_int(200));

    printf("Initial List Size: %zu\n", list_handle->length); // Outputs: 3

    // 5. Simulate data deletion by pulling a node out of the active chain
    ds_list_node_t *node_to_delete = ds_list_find(list_handle, ds_make_int(100), NULL);
    if (node_to_delete) {
        // Disconnects links and immediately inserts the node into the Arena's local Free-List
        ds_list_remove(&arena, list_handle, node_to_delete);
    }

    // 6. Run the Garbage Collector
    // Scans your root registries, traces graph/linear blocks, and purges orphans.
    // The dead '100' node is safely recycled back to the arena free stack.
    ds_arena_run_gc(&arena);

    // 7. Reallocate a new item
    // The allocator intercepts the top of the Free-List first, reusing the dead node
    // in O(1) time without advancing the arena's global bump pointer offset.
    ds_list_append(&arena, list_handle, ds_make_int(999));

    // Print Telemetry Reports to track memory reuse accuracy
    ds_arena_print_stats(&arena);

    // 8. Clean up everything safely before exit
    ds_arena_destroy(&arena); // Destroys all backing OS memory chunks
    ds_gc_destroy();          // Frees structural internal execution trackers

    printf("Execution completed cleanly.\n");
    return 0;
}
```
