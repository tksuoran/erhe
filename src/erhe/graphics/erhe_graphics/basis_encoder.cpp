#include "erhe_graphics/basis_encoder.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/ktx2_container.hpp"
#include "erhe_profile/profile.hpp"

#include "encoder/basisu_comp.h"
#include "transcoder/basisu_transcoder.h"

#include <cstring>
#include <mutex>
#include <string>

namespace erhe::graphics {

namespace {

[[nodiscard]] auto init_encoder() -> bool
{
    static std::once_flag once;
    static bool           ok{false};
    std::call_once(once, []() { ok = basisu::basisu_encoder_init(); });
    return ok;
}

[[nodiscard]] auto is_clamp(const Sampler_address_mode mode) -> bool
{
    return mode == Sampler_address_mode::clamp_to_edge;
}

} // anonymous namespace

auto is_basis_encoder_available() -> bool
{
    return true;
}

auto get_basis_encoder_version() -> std::string_view
{
    return BASISU_LIB_VERSION_STRING;
}

auto encode_image_to_basis_ktx2(
    const std::span<const std::uint8_t> encoded_image,
    const Texture_properties&           properties,
    const Basis_encode_settings&        settings
) -> std::vector<std::uint8_t>
{
    ERHE_PROFILE_FUNCTION();
    if (!init_encoder()) {
        log_texture->error("Basis encoder: basisu_encoder_init() failed");
        return {};
    }
    if (is_ktx2_basis(encoded_image) || is_ktx2_raw_blocks(encoded_image)) {
        log_texture->error("Basis encoder: the image is KTX2, not a PNG / JPEG");
        return {};
    }

    const bool srgb = (properties.transfer_function == Texture_transfer_function::srgb);
    // Premultiplied: the pixels a load of the image uploads (Image_loader default).
    Image_loader loader;
    Image_info   info;
    if (!loader.open(encoded_image, info, !srgb, Transcode_format_preference::rgba8, Alpha_mode::premultiplied)) {
        log_texture->error("Basis encoder: the image does not decode");
        return {};
    }
    if (
        ((info.format != erhe::dataformat::Format::format_8_vec4_srgb) && (info.format != erhe::dataformat::Format::format_8_vec4_unorm)) ||
        (info.width <= 0) || (info.height <= 0) || (info.level_count != 1) || (info.row_stride < (info.width * 4))
    ) {
        log_texture->error("Basis encoder: the image decodes to {} {}x{} with {} levels, not one RGBA8 level", erhe::dataformat::c_str(info.format), info.width, info.height, info.level_count);
        loader.close();
        return {};
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(info.row_stride) * static_cast<std::size_t>(info.height));
    const bool loaded = loader.load(pixels);
    loader.close();
    if (!loaded) {
        log_texture->error("Basis encoder: the image's pixels do not decode");
        return {};
    }

    const std::uint32_t width  = static_cast<std::uint32_t>(info.width);
    const std::uint32_t height = static_cast<std::uint32_t>(info.height);
    basisu::image image{width, height};
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* const row = pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(info.row_stride));
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* const p = row + (static_cast<std::size_t>(x) * 4u);
            image(x, y) = basisu::color_rgba{p[0], p[1], p[2], p[3]};
        }
    }

    // One thread: Export runs one encode per image in parallel.
    basisu::job_pool job_pool{1};
    basisu::basis_compressor_params params;
    params.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
    params.m_source_images.push_back(image);
    params.m_read_source_images            = false;
    params.m_write_output_basis_or_ktx2_files = false;
    params.m_create_ktx2_file              = true;
    params.m_ktx2_uastc_supercompression   = basist::KTX2_SS_ZSTANDARD;
    params.m_ktx2_zstd_supercompression_level = settings.zstd_level;
    params.m_pack_uastc_ldr_4x4_flags      = settings.uastc_level;
    params.m_multithreading                = false;
    params.m_pJob_pool                     = &job_pool;
    params.m_status_output                 = false;
    params.m_debug                         = false;
    params.m_compute_stats                 = false;
    params.m_validate_output_data          = false;
    params.set_srgb_options(srgb);
    params.m_mip_gen                       = properties.generate_mipmaps;
    params.m_mip_wrapping                  = !(is_clamp(properties.wrap_u) && is_clamp(properties.wrap_v));
    if (properties.normal_encoding == Texture_normal_encoding::three_component) {
        params.m_mip_renormalize = true;
    } else if (properties.normal_encoding == Texture_normal_encoding::two_component) {
        // X in RGB, Y in A (the layout transcode_ktx2_basis_to_raw_blocks() reads).
        params.m_swizzle[0] = 0;
        params.m_swizzle[1] = 0;
        params.m_swizzle[2] = 0;
        params.m_swizzle[3] = 1;
        basist::add_key_value(params.m_key_values, "KTXwriterScParams", std::string{ktx2_normal_mode_sc_params});
    }

    basisu::basis_compressor compressor;
    if (!compressor.init(params)) {
        log_texture->error("Basis encoder: the compressor does not initialize");
        return {};
    }
    const basisu::basis_compressor::error_code result = compressor.process();
    if (result != basisu::basis_compressor::cECSuccess) {
        log_texture->error("Basis encoder: encoding {}x{} failed with error {}", width, height, static_cast<int>(result));
        return {};
    }
    const basisu::uint8_vec& ktx2 = compressor.get_output_ktx2_file();
    return std::vector<std::uint8_t>{ktx2.data(), ktx2.data() + ktx2.size()};
}

} // namespace erhe::graphics
