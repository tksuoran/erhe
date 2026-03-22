# erhe_ui -- Code Review

## Summary
A font rendering and UI primitive library built on FreeType and HarfBuzz. The `Font` class handles glyph rasterization, atlas packing, and GPU text printing. The `Bitmap` class provides a software rasterization target. The `color_picker.cpp` is entirely inside `#if 0` -- it is dead code from an older UI system. The code is functional but shows its age in a few areas.

## Strengths
- Proper FreeType integration with outline/stroke support for bordered text
- HarfBuzz text layout for correct glyph shaping
- `Bitmap` class with template-based max-compositing blit operations
- `Rectangle` class with a complete utility API (hit testing, grow, shrink, clip)
- Atlas packing using `rbp::Rect` (Rectangle Bin Packing)
- Support for both rotated and non-rotated glyph atlas entries

## Issues
- **[moderate]** `color_picker.cpp` is 300 lines of completely dead code (`#if 0`). It references headers that likely no longer exist (`erhe_ui/gui_renderer.hpp`, `erhe_ui/ninepatch_style.hpp`, `erhe_ui/color_picker.hpp`). Should be removed entirely.
- **[moderate]** `Font` class (font.hpp:144-192) has raw C pointers for FreeType (`FT_LibraryRec_*`, `FT_FaceRec_*`) and HarfBuzz (`hb_font_t*`, `hb_buffer_t*`) resources. These are manually managed and could leak if an exception occurs during construction. RAII wrappers would be safer.
- **[moderate]** `Rectangle::set_size()` (rectangle.hpp:107) has a bug: `m_max.y = m_min.y + value.y + 1.0f` should be `m_min.y + value.y - 1.0f` (uses `+` instead of `-`, inconsistent with the x component which correctly uses `-`).
- **[minor]** `Bitmap::fill()` iterates pixel-by-pixel (bitmap.hpp:57-65) instead of using `std::fill` or `std::memset` on the underlying vector.
- **[minor]** `Bitmap` bounds checking in `put()` always runs (even in Release), while `get()` only checks in Debug. This inconsistency means Release builds pay for `put()` checks but not `get()` checks.
- **[minor]** `Font` has a fixed-size array `m_chars_256[256]` (font.hpp:166) -- this only handles ASCII/Latin-1 but the glyph map (`m_glyph_to_char`) handles arbitrary Unicode. The relationship between these is unclear.

## Fixed
- `Rectangle::set_size()` bug: `m_max.y = m_min.y + value.y + 1.0f` changed to `- 1.0f` to match x-component (rectangle.hpp:107)

## Suggestions
- Delete `color_picker.cpp` entirely -- it references nonexistent headers and is fully disabled
- Fix the `Rectangle::set_size()` bug: change `+ 1.0f` to `- 1.0f` for the y-component
- Use RAII wrappers (custom deleters with `std::unique_ptr`) for FreeType and HarfBuzz handles
- Use `std::memset` for `Bitmap::fill()` instead of per-pixel iteration
- Make bounds checking consistent between `put()` and `get()` -- either always check or only check in Debug for both
