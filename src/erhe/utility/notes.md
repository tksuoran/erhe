# erhe_utility

## Purpose
Small standalone utility classes and functions used across the erhe codebase: memory alignment helpers, bitwise test functions, a fixed-size pimpl smart pointer, and an interned debug label type backed by a thread-safe string pool.

## Key Types
- `Debug_label` -- Lightweight immutable string wrapper backed by `String_pool`. Stores a `string_view` pointing into an interned pool, avoiding allocations for repeated labels. Supports construction from string literals (constexpr), `string_view`, and `std::string`.
- `String_pool` -- Singleton thread-safe string interning pool using `unordered_set<string>` with transparent hashing. Accessed via `String_pool::instance().intern(sv)`.
- `pimpl_ptr<T, Size, Align>` -- Fixed-size, stack-allocated pimpl holder. Stores `T` in an internal `std::byte[Size]` buffer using placement new. Supports copy, move, and swap. Avoids heap allocation for pimpl patterns.

## Public API
- `align_offset_power_of_two(offset, alignment)` -- Aligns offset up to power-of-two boundary.
- `align_offset_non_power_of_two(offset, alignment)` -- Aligns offset up to arbitrary alignment.
- `next_power_of_two(x)` -- Returns next power of two >= x (32-bit).
- `test_bit_set(lhs, rhs)` / `test_all_rhs_bits_set()` / `test_any_rhs_bits_set()` -- Bitwise flag testing helpers.

## Dependencies
- erhe::verify (used by align.hpp for ERHE_VERIFY assertions)
- No other erhe libraries.

## Notes
- `Debug_label` is used extensively throughout erhe for naming GPU objects, render graph nodes, and other resources without runtime string allocation overhead.
- `pimpl_ptr` requires the user to specify the exact `Size` and `Align` at compile time; a size mismatch will cause undefined behavior.
- This library has no external dependencies beyond the standard library and erhe::verify.
