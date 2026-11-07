#pragma once

#include "erhe_graphics/texture_properties.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Basis Universal encoder (doc/erhe/graphics.md "Basis Universal encoder"):
// a PNG / JPEG image to a Basis Universal KTX2 file, built with ERHE_BASIS_ENCODER.
namespace erhe::graphics {

// The encoder settings a Basis Universal KTX2 file is made with beside the
// texture properties; a Basis texture item's key names every field.
class Basis_encode_settings
{
public:
    std::uint32_t uastc_level{2}; // UASTC LDR 4x4 pack level 0 (fastest) - 4 (very slow); 2 is basisu's default
    int           zstd_level {6}; // KTX2 Zstandard supercompression level of the UASTC data
};

// False in a build without ERHE_BASIS_ENCODER (Android).
[[nodiscard]] auto is_basis_encoder_available() -> bool;
// The basisu library version ("2.50"); empty without the encoder.
[[nodiscard]] auto get_basis_encoder_version() -> std::string_view;

// Decodes a PNG / JPEG image (RGBA8, alpha premultiplied as a load of the
// image uploads it) and encodes it as UASTC LDR 4x4 KTX2, Zstandard
// supercompressed, with mip levels when the properties generate them (wrapping
// at the edges unless both wrap modes clamp), in the transfer function of the
// properties; a three component normal map has its mips renormalized, a two
// component one is stored X in RGB, Y in A with the --normal-mode marker.
// Empty on failure (logged) and without the encoder. Any thread.
[[nodiscard]] auto encode_image_to_basis_ktx2(
    std::span<const std::uint8_t> encoded_image,
    const Texture_properties&     properties,
    const Basis_encode_settings&  settings
) -> std::vector<std::uint8_t>;

} // namespace erhe::graphics
