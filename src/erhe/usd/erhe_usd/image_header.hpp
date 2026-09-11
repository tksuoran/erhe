#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace erhe::usd {

// What an image file says about its own texels, read from its header alone:
// no decoding. This is what the UsdPreviewSurface `sourceColorSpace = auto`
// rule needs (is_srgb_by_auto_rule): an image is color when it is 8-bit and
// carries 3 or 4 components, and data otherwise. A palette image counts as
// 8-bit RGB, which is what every decoder expands it to.
//
// Recognized containers: PNG, JPEG, BMP and TGA. Any other file, or a header
// too short or malformed to read, gives `known == false`.
class Image_header final
{
public:
    bool        known             {false};
    std::size_t bits_per_component{0};
    std::size_t component_count   {0};
};

[[nodiscard]] auto probe_image_header(std::span<const std::uint8_t> bytes) -> Image_header;

// Reads only as much of the file as the header needs: a fixed prefix first,
// then the whole file when a JPEG's metadata segments push its frame header
// past that prefix. A file that cannot be opened gives `known == false`.
[[nodiscard]] auto probe_image_header(const std::filesystem::path& path) -> Image_header;

// The `auto` rule of the UsdPreviewSurface specification: sRGB when the
// image is 8-bit with 3 or 4 components. An unreadable header is not sRGB.
[[nodiscard]] auto is_srgb_by_auto_rule(const Image_header& header) -> bool;

} // namespace erhe::usd
