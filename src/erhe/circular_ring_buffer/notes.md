# erhe_circular_ring_buffer

## Purpose

Provides `Circular_ring_buffer_algorithm`: the pure-arithmetic core of a
circular ring buffer with producer/consumer wrap counts and deferred,
frame-indexed reclamation. The class has no GPU or `Buffer` dependency; it
manages only offsets, wrap counts, and a vector of pending sync entries. Used
by `erhe::graphics::Ring_buffer` to perform allocation bookkeeping, and
intended for reuse by any future circular streaming buffer (CPU-side,
networking, non-GPU staging).

## Key Types

- `Circular_ring_buffer_algorithm` -- Fixed-capacity circular allocator with a
  monotonically advancing write position (wrapping back to 0 on overflow) and
  a read position advanced by `frame_completed()` callbacks. Each `acquire()`
  returns the chosen wrap count and byte offset; the caller pairs the wrap
  count back with `make_sync_entry()` at submission time so a subsequent
  `frame_completed()` knows how far the read position can advance.
- `Circular_ring_buffer_algorithm::Allocation` -- Value returned by `acquire`:
  `{ wrap_count, byte_offset, byte_count }`.

## Public API

- `acquire(required_alignment, byte_count)` -- Returns
  `std::optional<Allocation>` or `nullopt` if no segment can satisfy the
  request. `byte_count == 0` means "as much as possible in the current segment".
- `get_size_available_for_write(alignment, &align_pad, &avail_no_wrap,
  &avail_with_wrap)` -- Queries free space without mutating state.
- `make_sync_entry(frame_index, wrap_count, byte_offset, byte_count)` --
  Registers an in-flight range that the GPU (or other downstream consumer)
  must finish reading before its bytes become writable again. Calls within
  the same `(frame_index, wrap_count)` merge into a single union range.
- `frame_completed(completed_frame)` -- Advances the read position past every
  sync entry whose `waiting_for_frame == completed_frame`, then erases those
  entries.
- `assert_invariants()` -- Public so tests can check between steps.

## Invariant

Either `write_wrap_count == read_wrap_count + 1` with `read_offset >=
write_position`, or `write_wrap_count == read_wrap_count` with
`write_position >= read_offset`. The constructor initialises
`read_offset = capacity_byte_count` and `write_wrap_count = 1`, so the very
first call to `get_size_available_for_write` lands in the first branch and
reports `capacity` bytes available without wrap.

## Dependencies

- erhe::utility (PRIVATE, header-only consumption of
  `align_offset_power_of_two`).
- erhe::verify (PRIVATE, `ERHE_VERIFY` / `ERHE_FATAL` macros).
- No other erhe libraries. No external dependencies.

## Notes

- Single-threaded. The class has no mutex and uses no atomics; it expects to
  be driven from one thread (the render thread, in the `erhe::graphics`
  case).
- The class does not touch any storage; it only manages bookkeeping. The
  bytes themselves live in whatever buffer the caller pairs with it.
