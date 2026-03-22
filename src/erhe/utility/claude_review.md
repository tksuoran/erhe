# erhe_utility -- Code Review

## Summary
A small collection of utility primitives: alignment helpers, bit test helpers, a string-interning debug label system, and a stack-based pimpl pointer. The code is clean, modern C++20, and well-implemented. The `Debug_label` with `String_pool` interning is a nice optimization for pervasive debug labeling.

## Strengths
- `Debug_label` with `String_pool` interning avoids redundant string allocations while keeping the API simple
- `String_pool` uses heterogeneous lookup (`is_transparent`) for efficient lookups without temporary allocations
- `pimpl_ptr` uses `std::construct_at`/`std::destroy_at` and `std::launder` correctly for in-place storage
- `Debug_label` has a `constexpr` constructor for string literals that avoids the string pool entirely
- Alignment functions are `inline` in the header, avoiding link-time overhead
- Bit helper templates are clean and generic

## Issues
- **[minor]** `align_offset_non_power_of_two` comment (align.hpp:23-24) references the power-of-two formula as "NOTE: This only works for power of two alignments" but then shows the correct non-power-of-two implementation below. The comment is potentially misleading -- it should say "the formula below works for any alignment".
- **[minor]** `pimpl_ptr` does not have a `static_assert` verifying that `Size` is large enough for `T` and `Align` matches `alignof(T)`. A mismatched size would cause UB that is very hard to debug.
- **[minor]** `align.cpp` and `bit_helpers.cpp` are empty 1-byte files. The `.cpp` files are unnecessary since all code is in headers -- they exist only to satisfy CMake source file requirements.
- **[minor]** `test_bit_set` and `test_all_rhs_bits_set` (bit_helpers.hpp:6-14) have identical implementations. One is redundant.

## Suggestions
- Add `static_assert(sizeof(T) <= Size && alignof(T) <= Align)` to `pimpl_ptr` constructor
- Remove the redundant `test_bit_set` function (or alias it to `test_all_rhs_bits_set`)
- Clarify the comment in `align_offset_non_power_of_two` about the formula applicability
