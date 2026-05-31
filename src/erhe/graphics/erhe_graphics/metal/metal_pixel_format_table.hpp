#pragma once

// ---------------------------------------------------------------------------
// Metal pixel-format texture-capability table.
//
// This is a standalone translation of Apple's "Metal Feature Set Tables"
// document (section: "Texture capabilities by pixel format"). It encodes, for
// every documented MTLPixelFormat, which texture capabilities each GPU family
// supports.
//
// The unit is intentionally self-contained so it can be lifted into a separate
// library: the header depends only on <cstdint> and a forward declaration of
// MTL::Device; the implementation (.cpp) depends only on metal-cpp
// (<Metal/Metal.hpp>) plus this header. It has no erhe dependencies.
//
// Source of truth: Metal-Feature-Set-Tables.pdf (Apple, dated in-document
// "February 5, 2026"). The capability legend (verbatim from the PDF):
//
//   * Atomic : atomic operations on textures with the pixel format
//   * Filter : the GPU can filter the texture during sampling
//   * Write  : per-pixel shader write (read_write / write textures)
//   * Color  : usable as a color render target
//   * Blend  : the GPU can blend a texture with the pixel format
//   * MSAA   : usable as a multisample (MSAA) render target
//   * Resolve: usable as the source of an MSAA resolve
//   * Sparse : sparse-texture allocations are supported
//   * "All"  : Filter + Write + Color + Blend + MSAA + Resolve (+ Sparse on
//              families that support sparse: Metal4 and Apple6..Apple10; per
//              the PDF, Sparse is NOT included in "All" for Mac2, Metal3 and
//              Apple2..Apple5).
//
// See scripts/metal_format_tables/generate.py for the authoring source that
// produced the data table in the .cpp.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace MTL {
    class Device;
}

namespace erhe::graphics::metal {

// GPU families, in the column order used by the Apple feature-set tables.
enum class Gpu_family : int {
    metal3 = 0,
    metal4,
    apple2,
    apple3,
    apple4,
    apple5,
    apple6,
    apple7,
    apple8,
    apple9,
    apple10,
    mac2,
    count
};

// Texture capability bit flags, matching the PDF legend above.
enum Texture_capability : std::uint32_t {
    texture_capability_none    = 0u,
    texture_capability_atomic  = 1u << 0,
    texture_capability_filter  = 1u << 1,
    texture_capability_write   = 1u << 2, // color/storage per-pixel write
    texture_capability_color   = 1u << 3, // color render target
    texture_capability_blend   = 1u << 4,
    texture_capability_msaa    = 1u << 5, // multisample render target
    texture_capability_resolve = 1u << 6, // MSAA resolve source
    texture_capability_sparse  = 1u << 7
};

// Returns the bitwise-OR of Texture_capability flags that the given GPU family
// supports for the given MTLPixelFormat (passed as its raw NSUInteger value).
// Returns texture_capability_none for unknown formats, or for formats that are
// "Not available" on that family.
[[nodiscard]] auto metal_pixel_format_texture_capabilities(
    std::uint64_t mtl_pixel_format,
    Gpu_family    family
) noexcept -> std::uint32_t;

// True if the pixel format appears in the table at all (on any family).
[[nodiscard]] auto metal_pixel_format_is_known(std::uint64_t mtl_pixel_format) noexcept -> bool;

// Human-readable family name (e.g. "Apple7"), for logging.
[[nodiscard]] auto metal_gpu_family_name(Gpu_family family) noexcept -> const char*;

// Picks the most capable Gpu_family that the device reports support for. Apple
// silicon reports both an Apple family and Mac2; the Apple family is a superset,
// so it is preferred. Falls back to mac2, then metal4 / metal3.
[[nodiscard]] auto metal_resolve_gpu_family(MTL::Device* device) noexcept -> Gpu_family;

} // namespace erhe::graphics::metal
