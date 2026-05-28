# Extract Circular_ring_buffer_algorithm into erhe::circular_ring_buffer

## Context

`erhe::graphics::Ring_buffer` (`src/erhe/graphics/erhe_graphics/ring_buffer.hpp`
and `.cpp`) is a circular GPU-streaming buffer with fence-based synchronization.
Inside it lives a self-contained piece of pure arithmetic: read/write positions,
wrap counts, alignment-aware allocation, and a vector of "in-flight" sync entries
keyed by frame index. Today that logic is interleaved with `Device`, `Buffer`,
persistent-mapping, and CPU-shadow-buffer code, so the only way to exercise it
is to spin up a full graphics device.

The goal is to lift the position/wrap/sync-entry logic into a new dedicated
library, `erhe::circular_ring_buffer`, containing one class,
`Circular_ring_buffer_algorithm`. That class becomes:

1. **Testable in isolation.** A dedicated `erhe_circular_ring_buffer_tests`
   GoogleTest target, modelled on the existing `erhe_graphics_tests` and
   `erhe_math_tests`.
2. **Reusable.** Any future circular streaming buffer (CPU-side, networking,
   non-GPU staging) can hold one without dragging in `erhe::graphics::Device`.
3. **Easier to reason about.** The invariant on `(write_wrap_count,
   read_wrap_count, write_position, read_offset)` becomes the only thing the
   new class enforces; `Ring_buffer` keeps the GPU-tied responsibilities.

The refactor is behaviour-preserving at every existing caller
(`Ring_buffer_client` subclasses, `Ring_buffer_range::release()`, and the
backend `frame_completed()` calls in `gl_device.cpp`, `vulkan_device.cpp`,
`metal_device.cpp`). It is not a rewrite; the new class hosts the same code
paths verbatim, just relocated and stripped of `Device`/`Buffer` references.

## New library layout

```
src/erhe/circular_ring_buffer/
    CMakeLists.txt
    notes.md
    erhe_circular_ring_buffer/
        circular_ring_buffer_algorithm.hpp
        circular_ring_buffer_algorithm.cpp
    test/
        CMakeLists.txt
        main.cpp
        test_circular_ring_buffer_algorithm.cpp
```

- Namespace: `erhe::circular_ring_buffer`
- CMake target: `erhe_circular_ring_buffer`, aliased as
  `erhe::circular_ring_buffer`
- Test target: `erhe_circular_ring_buffer_tests`
- Library dependencies:
  - PUBLIC: none beyond standard library.
  - PRIVATE: `erhe::utility` (for `align_offset_power_of_two`) and
    `erhe::verify`.
- `erhe::graphics` adds `erhe::circular_ring_buffer` to its `PRIVATE` link
  libraries (only `ring_buffer.cpp` includes the new header).

## Class design

### Public API

```cpp
class Circular_ring_buffer_algorithm
{
public:
    explicit Circular_ring_buffer_algorithm(std::size_t capacity_byte_count);

    class Allocation
    {
    public:
        std::size_t wrap_count {0};
        std::size_t byte_offset{0};
        std::size_t byte_count {0};
    };

    void get_size_available_for_write(
        std::size_t  required_alignment,
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;

    [[nodiscard]] auto acquire(
        std::size_t required_alignment,
        std::size_t byte_count
    ) -> std::optional<Allocation>;

    void make_sync_entry(
        std::uint64_t frame_index,
        std::size_t   wrap_count,
        std::size_t   byte_offset,
        std::size_t   byte_count
    );

    void frame_completed(std::uint64_t completed_frame);

    [[nodiscard]] auto get_capacity_byte_count() const -> std::size_t;
    [[nodiscard]] auto get_write_position     () const -> std::size_t;
    [[nodiscard]] auto get_write_wrap_count   () const -> std::size_t;
    [[nodiscard]] auto get_read_offset        () const -> std::size_t;
    [[nodiscard]] auto get_read_wrap_count    () const -> std::size_t;
    [[nodiscard]] auto get_sync_entry_count   () const -> std::size_t;

    void assert_invariants() const;
};
```

`make_sync_entry` takes `frame_index` explicitly; the outer `Ring_buffer` passes
`m_device.get_frame_index()` at the call site. That is the only semantic change
at the boundary, and it is what makes the new class device-free.

### What `Ring_buffer` becomes

`Ring_buffer` keeps `Device&`, `Ring_buffer_usage`, `m_buffer`, `m_cpu_buffer`,
`m_map_offset`, plus the `flush()`, `close()`, `match()`, `get_buffer()`, and
`get_name()` GPU-side functions. It gains one new member:

```cpp
erhe::circular_ring_buffer::Circular_ring_buffer_algorithm m_algorithm;
```

`Ring_buffer::acquire` first calls `m_algorithm.acquire(...)` for the
allocation, then slices its `Buffer` map / CPU shadow at the returned offset
and forwards to `Ring_buffer_range`. `Ring_buffer::make_sync_entry`,
`Ring_buffer::frame_completed`, `Ring_buffer::get_size_available_for_write`,
and `Ring_buffer::sanity_check` become one-line forwarders. `Ring_buffer_range`
is unchanged.

Dead-code cleanup performed as part of the same change:

- Remove `m_last_write_wrap_count` from `ring_buffer.hpp` and from the
  uncompiled `vulkan_ring_buffer.hpp` duplicate. Field is unused.
- Remove the orphan `class Ring_buffer_impl;` forward declaration in
  `ring_buffer.hpp`.

## Verification

### Build

```
scripts\configure_vs2026_opengl.bat
```

Then build `erhe_circular_ring_buffer`, `erhe_circular_ring_buffer_tests`, and
`editor` in the generated `build_vs2026_opengl/` solution.

### Run the new unit tests

`erhe_circular_ring_buffer_tests.exe` exercises only the new class. Cases:

1. `initial_state`
2. `acquire_unaligned_advance`
3. `acquire_alignment_padding_consumed`
4. `acquire_zero_byte_count_takes_max_segment`
5. `acquire_triggers_wrap_when_no_room_at_end`
6. `acquire_returns_nullopt_when_full`
7. `make_sync_entry_dedup_same_frame_same_wrap`
8. `make_sync_entry_distinct_frames_appended`
9. `frame_completed_advances_read_pointer`
10. `frame_completed_no_match_is_no_op`
11. `wrap_cycle_invariant_holds`
12. `acquire_after_partial_read_uses_freed_tail_only`

### Smoke-test the editor

Launch `editor` (Visual Studio 2026, OpenGL configuration). The in-flight
allocation path should be visibly identical to before the refactor.

## Risks / things to double-check

- **`Ring_buffer_impl` in `vulkan/` is uncompiled dead code.** Confirmed not in
  `src/erhe/graphics/CMakeLists.txt`. Out of scope for this change beyond
  removing its dead `m_last_write_wrap_count` field.
- **Initial `m_read_offset = capacity`** is load-bearing. With it, the very
  first `get_size_available_for_write` lands in the `write_wrap_count (1) ==
  read_wrap_count (0) + 1` branch and reports `capacity` bytes available
  without wrap. Initialising to 0 would silently flip the invariant.
- **`make_sync_entry` merge condition** uses `byte_offset + byte_count >
  entry.byte_offset + entry.byte_count`, then overwrites both fields with the
  new range. Equal/contained ranges are skipped. Preserve exactly.
- **`frame_completed` two-pass behaviour** -- update read state first, then
  `std::remove_if` strips every entry whose `waiting_for_frame ==
  completed_frame`, even ones it did not pick as the largest. Preserve.
- **Single-threaded** -- no mutex, no atomics, like the existing `Ring_buffer`.
