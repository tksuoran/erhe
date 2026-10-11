# erhe_utility

Stability: stable

## Purpose
Small standalone utility classes and functions used across the erhe codebase: memory alignment helpers, bitwise test functions, a fixed-size pimpl smart pointer, an interned debug label type backed by a thread-safe string pool, and the process-wide thread roles / frame phase asserts.

## Key Types
- `Debug_label` -- Lightweight immutable string wrapper backed by `String_pool`. Stores a `string_view` pointing into an interned pool, avoiding allocations for repeated labels. Supports construction from string literals (constexpr), `string_view`, and `std::string`.
- `String_pool` -- Singleton thread-safe string interning pool using `unordered_set<string>` with transparent hashing. Accessed via `String_pool::instance().intern(sv)`.
- `Thread_roles` -- Process-wide thread roles (main thread, render thread) and one `Frame_phase` per role (`none`, `m1_overlap`, `m2_prepare`, `r_render`; the editor's frame phases are described in `doc/editor/editor.md` "Frame phases and the render thread"): the main phase and the render phase. The calling role is the render role on a dedicated render thread (render thread set and different from the main thread) and the main role on every other thread; in inline mode (render thread == main thread) the main thread holds both roles and writes both phases. Static functions only; plain storage without locks: the ids are set once before other threads read them, and each phase is written and read by one thread only. Libraries call its asserts at their thread-ownership choke points; an application that never sets the roles (tests, examples) gets no-op asserts.
- `pimpl_ptr<T, Size, Align>` -- Fixed-size, stack-allocated pimpl holder. Stores `T` in an internal `std::byte[Size]` buffer using placement new. Supports copy, move, and swap. Avoids heap allocation for pimpl patterns.

## Public API
- `align_offset_power_of_two(offset, alignment)` -- Aligns offset up to power-of-two boundary.
- `align_offset_non_power_of_two(offset, alignment)` -- Aligns offset up to arbitrary alignment.
- `next_power_of_two(x)` -- Returns next power of two >= x (32-bit).
- `test_bit_set(lhs, rhs)` / `test_all_rhs_bits_set()` / `test_any_rhs_bits_set()` -- Bitwise flag testing helpers.
- `Thread_roles::set_main_thread(id)` / `set_render_thread(id)` / `set_frame_phase(phase)` and the matching getters -- Publish the roles and the calling role's phase (`get_frame_phase()` returns the calling role's phase). The editor sets the main role at construction and the render role before the first frame (in inline mode the main thread at the start of `run()`; in dedicated mode the render thread itself, inside the `Render_thread` constructor handshake), the main phase at the edges of M1 and M2, and the render phase at the start and end of R from the thread that runs R.
- `Thread_roles::is_render_role()` -- True when the calling role is the render role (a dedicated render thread); false on the main thread, worker threads, in inline mode and while the roles are unset. `erhe::graphics::Device::get_frame_index()` keys the frame it reports on it.
- `Thread_roles::assert_main_thread()` / `assert_render_thread()` -- `ERHE_VERIFY` the calling thread holds the role; no-op while that role is unset.
- `Thread_roles::assert_frame_phase(phase)` -- Fatal when the calling role's phase differs; no-op until the render role is set (start-up and the editor's init pump run outside the frame model).
- `Thread_roles::assert_main_thread_in_phase(phase)` -- Main thread, and the main phase is the given one or `none` (outside any frame: start-up, scene load, shutdown).
- `Thread_roles::assert_command_recording_allowed()` -- Fatal when the main thread obtains a command buffer while the main phase is `m1_overlap`; called by `erhe::graphics::Device::get_command_buffer`.

## Dependencies
- erhe::verify (linked PUBLIC; ERHE_VERIFY in align.hpp and the Thread_roles asserts)
- No other erhe libraries.

## Notes
- `Debug_label` is used extensively throughout erhe for naming GPU objects, render graph nodes, and other resources without runtime string allocation overhead.
- `pimpl_ptr` requires the user to specify the exact `Size` and `Align` at compile time; a size mismatch will cause undefined behavior.
- This library has no external dependencies beyond the standard library and erhe::verify.
