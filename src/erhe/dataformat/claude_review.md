# erhe_dataformat -- Code Review

## Summary
A solid data format conversion library providing pixel format definitions, norm/unorm conversions, sRGB conversions, and a vertex format abstraction. The conversion code is well-tested domain knowledge. The vertex format API is clean and practical. The main concern is the very large format enum and the extensive switch statement in the implementation.

## Strengths
- Comprehensive format enum covering all common GPU formats including packed and depth/stencil
- Correct sRGB-to-linear and linear-to-sRGB conversions following the standard formulas
- Norm conversion functions handle edge cases (clamping, rounding) correctly
- `Vertex_format` and `Vertex_stream` provide a clean, practical API for vertex buffer layout
- Good use of `[[nodiscard]]` on query functions
- The `convert()` function handles cross-format conversion with proper scaling

## Issues
- **[minor]** `linear_rgb_to_srgb()` (dataformat.cpp:29) compares a `float` against `0.0` (double literal) instead of `0.0f`. This is harmless but inconsistent.
- **[minor]** The `Format` enum uses a `format_` prefix on every value (e.g., `format_8_scalar_srgb`). Since the enum is scoped (`enum class`), this prefix is redundant.
- **[minor]** `Vertex_format::Vertex_format()` default constructor (vertex_format.cpp:56-58) has an empty body -- could use `= default`.
- **[minor]** `find_attribute` methods use linear search which is fine for typical small vertex formats but could become O(n*m) in aggregate lookups.

## Suggestions
- Consider removing the `format_` prefix from enum values since `Format::` already provides the namespace.
- The large `convert()` function in `dataformat.cpp` could benefit from a lookup table approach instead of nested switch statements to reduce code size and improve maintainability.
- Use `= default` for the empty `Vertex_format` default constructor.
- Add `constexpr` where possible on the utility functions like `get_format_size_bytes`.
