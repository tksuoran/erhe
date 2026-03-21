# erhe_ui

## Purpose
Font rasterization and text layout utilities. Renders TrueType/OpenType fonts into GPU texture atlases using FreeType for glyph rasterization and HarfBuzz for text shaping. Provides glyph measurement, text printing into vertex buffers, and a bitmap class for CPU-side image manipulation. Used by `erhe::renderer::Text_renderer` for in-viewport text.

## Key Types
- `Font` -- Loads a font file, rasterizes glyphs into a texture atlas, and provides `print()` (writes glyph quads to a uint32 buffer), `measure()` (returns bounding rectangle), and `get_glyph_count()`. Configurable hinting, DPI, gamma, bolding, and outline thickness.
- `Bitmap` -- CPU-side pixel buffer with multi-component support (1-4 channels). Provides `put()`, `get()`, `blit()` (with optional max-blend and rotation), `fill()`, `post_process()` (premultiplied alpha), and `as_span()`.
- `Glyph` -- (FreeType only) Represents a single rasterized glyph with its metrics, bitmap layout, and atlas rectangle placement.
- `Rectangle` -- 2D axis-aligned bounding box with min/max corners. Supports hit testing, extend, clip, shrink, and grow operations.

## Public API
- Construct `Font(device, path, size, outline_thickness)`, then call `render()` to rasterize.
- `font.print(buffer, text, position, color, out_bounds)` writes glyph quads.
- `font.measure(text)` returns the bounding `Rectangle`.
- `Bitmap` is used internally for glyph atlas composition.

## Dependencies
- erhe::graphics (Device, Texture -- for the font atlas texture)
- erhe::verify (ERHE_VERIFY, ERHE_FATAL)
- FreeType (conditional, `ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE`)
- HarfBuzz (conditional, `ERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ`)
- RectangleBinPack (rbp::Rect for atlas packing)
- glm, fmt

## Notes
- Font rendering is optional; if FreeType is not available, font features are disabled.
- The `Glyph` class is only compiled when FreeType is available.
- Outline support rasterizes two passes (fill + stroke) for outlined text rendering.
