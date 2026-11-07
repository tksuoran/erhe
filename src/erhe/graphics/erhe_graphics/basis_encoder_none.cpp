#include "erhe_graphics/basis_encoder.hpp"
#include "erhe_graphics/graphics_log.hpp"

// A build without ERHE_BASIS_ENCODER (Android): nothing encodes.
namespace erhe::graphics {

auto is_basis_encoder_available() -> bool
{
    return false;
}

auto get_basis_encoder_version() -> std::string_view
{
    return {};
}

auto encode_image_to_basis_ktx2(
    const std::span<const std::uint8_t>,
    const Texture_properties&,
    const Basis_encode_settings&
) -> std::vector<std::uint8_t>
{
    log_texture->error("Basis encoder: this build has no Basis Universal encoder (ERHE_BASIS_ENCODER is off)");
    return {};
}

} // namespace erhe::graphics
