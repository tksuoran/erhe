# erhe_dataformat

## Purpose
Graphics-API-agnostic pixel and vertex data format definitions. Provides a comprehensive
`Format` enum covering 8/16/32-bit scalar through vec4 types in various numeric representations
(unorm, snorm, uint, sint, float, sRGB), packed formats, and depth/stencil formats. Also
provides vertex format descriptions for building vertex attribute layouts.

## Key Types
- `Format` -- Enum with ~70 entries covering all common pixel/vertex data formats.
- `Format_kind` -- Classifies a format as unsigned_integer, signed_integer, float, or depth_stencil.
- `Vertex_attribute` -- A single vertex attribute: format, usage type, usage index, byte offset.
- `Vertex_attribute_usage` -- Enum: position, tangent, bitangent, normal, color, joint_indices, joint_weights, tex_coord, custom.
- `Vertex_stream` -- A collection of attributes sharing a binding and stride.
- `Vertex_format` -- A collection of vertex streams forming a complete vertex layout.

## Public API
- `get_format_size_bytes(format)` / `get_component_count(format)` -- Query format properties.
- `convert(src, src_format, dst, dst_format, scale)` -- Convert between formats.
- `float_to_snorm16()` / `pack_unorm4x8()` / etc. -- Packing/conversion utilities.
- `srgb_to_linear()` / `linear_rgb_to_srgb()` -- Color space conversions.
- `Vertex_format::find_attribute(usage_type, index)` -- Look up an attribute in the format.

## Dependencies
- **erhe libraries:** `erhe::log`, `erhe::verify`
- **External:** fmt

## Notes
- This library is purely data-description; it has no GPU dependencies.
- Used by both `erhe::gl` and `erhe::graphics` to bridge format descriptions.
- Custom attribute indices are defined as constants (e.g. `custom_attribute_id`).
