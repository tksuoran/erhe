#include "erhe_graphics/png_loader_mango_spng.hpp"
#include "erhe_verify/verify.hpp"

#include <igl/TextureFormat.h>

#include "spng.h"

#include <mango/mango.hpp>

#include <filesystem>

namespace erhe::graphics
{

[[nodiscard]] auto from_spng(const enum ::spng_format format) -> igl::TextureFormat
{
    if (format == SPNG_FMT_RGB8) {
        return igl::TextureFormat::RGBX_UNorm8;
    }

    if (format == SPNG_FMT_RGBA8) {
        return igl::TextureFormat::RGBA_SRGB;
    }

    ERHE_FATAL("unsupported PNG image color type");
}

[[nodiscard]] auto to_spng(const igl::TextureFormat format) -> enum ::spng_format
{
    //using Format = mango::image::Format;

    switch (format) {
        //using enum gl::Internal_format;
        case igl::TextureFormat::RGBX_UNorm8: return ::spng_format::SPNG_FMT_RGB8;
        case igl::TextureFormat::RGBA_SRGB:   return ::spng_format::SPNG_FMT_RGBA8;
        default: break;
    }

    ERHE_FATAL("unsupported PNG image color type");
}

[[nodiscard]] auto to_spng(const Image_format format) -> enum ::spng_format
{
    switch (format) {
        //using enum Image_format;
        case Image_format::srgb8:        return ::spng_format::SPNG_FMT_RGB8;
        case Image_format::srgb8_alpha8: return ::spng_format::SPNG_FMT_RGBA8;
        default: break;
    }

    ERHE_FATAL("unsupported PNG image color type");
}

PNG_loader::PNG_loader()
{
}

PNG_loader::~PNG_loader() noexcept
{
    close();
}

void PNG_loader::close()
{
    ::spng_ctx_free(m_image_decoder);
    m_image_decoder = nullptr;
    m_file.reset();
}

auto PNG_loader::open(
    const std::filesystem::path& path,
    Image_info&                  info
) -> bool
{
    close();

    m_file = std::make_unique<mango::filesystem::File>(path.string());
    mango::filesystem::File& file = *m_file;
    m_image_decoder = ::spng_ctx_new(0);
    if (m_image_decoder == nullptr) {
        m_file.reset();
        return false;
    }

    int result{};
    result = ::spng_set_png_buffer(m_image_decoder, file.data(), file.size());
    if (result != 0) {
        return false;
    }

    result = ::spng_decode_chunks(m_image_decoder);
    if (result != 0) {
        return false;
    }

    struct ::spng_ihdr ihdr{
        .width      = 0,
        .height     = 0,
        .bit_depth  = 0,
        .color_type = SPNG_COLOR_TYPE_GRAYSCALE
    };
    result = ::spng_get_ihdr(m_image_decoder, &ihdr);
    if (result != 0) {
        return false;
    }

    std::size_t image_size{};
    result = ::spng_decoded_image_size(m_image_decoder, SPNG_FMT_RGBA8, &image_size);
    if (result != 0) {
        return false;
    }

    info.width       = ihdr.width;
    info.height      = ihdr.height;
    info.depth       = 1;
    info.level_count = 1;
    info.row_stride  = ihdr.width * 4;
    info.format      = Image_format::srgb8_alpha8;

    return true;
}

auto PNG_loader::open(
    const gsl::span<const std::byte>& buffer_view,
    Image_info&                       info
) -> bool
{
    close();

    m_image_decoder = ::spng_ctx_new(0);
    if (m_image_decoder == nullptr) {
        return false;
    }

    int result{};
    result = ::spng_set_png_buffer(m_image_decoder, buffer_view.data(), buffer_view.size());
    if (result != 0) {
        return false;
    }

    result = ::spng_decode_chunks(m_image_decoder);
    if (result != 0) {
        return false;
    }

    struct ::spng_ihdr ihdr{
        .width      = 0,
        .height     = 0,
        .bit_depth  = 0,
        .color_type = SPNG_COLOR_TYPE_GRAYSCALE
    };
    result = ::spng_get_ihdr(m_image_decoder, &ihdr);
    if (result != 0) {
        return false;
    }

    std::size_t image_size{};
    result = ::spng_decoded_image_size(m_image_decoder, SPNG_FMT_RGBA8, &image_size);
    if (result != 0) {
        return false;
    }

    info.width       = ihdr.width;
    info.height      = ihdr.height;
    info.depth       = 1;
    info.level_count = 1;
    info.row_stride  = ihdr.width * 4;
    info.format      = Image_format::srgb8_alpha8;

    return true;
}

auto PNG_loader::load(gsl::span<std::byte> transfer_buffer) -> bool
{
    int result = ::spng_decode_image(m_image_decoder, transfer_buffer.data(), transfer_buffer.size(), SPNG_FMT_RGBA8, 0);
    return (result == 0);
}

PNG_writer::PNG_writer()
{
    m_image_encoder = ::spng_ctx_new(SPNG_CTX_ENCODER);
}

PNG_writer::~PNG_writer() noexcept
{
    ::spng_ctx_free(m_image_encoder);
}

int spng_rw(spng_ctx* ctx, void* user, void* dst_src, ::size_t length)
{
    static_cast<void>(ctx);
    auto* writer = reinterpret_cast<PNG_writer*>(user);
    return writer->stream_op(dst_src, length);
}

auto PNG_writer::stream_op(void* dst_src, std::size_t length) -> int
{
    m_file_stream->write(dst_src, length);
    return 0;
}

auto PNG_writer::write(
    const std::filesystem::path& path,
    const Image_info&            info,
    gsl::span<std::byte>         data
) -> bool
{
    if (m_image_encoder == nullptr) {
        return false;
    }

    int result{};
    result = ::spng_set_option(m_image_encoder, SPNG_ENCODE_TO_BUFFER, 0);
    if (result != 0) {
        return false;
    }

    result = ::spng_set_png_stream(m_image_encoder, spng_rw, this);
    if (result != 0) {
        return false;
    }

    /* Specify image dimensions, PNG format */
    struct ::spng_ihdr ihdr{
        .width      = static_cast<uint32_t>(info.width),
        .height     = static_cast<uint32_t>(info.height),
        .bit_depth  = 8,
        .color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA
    };

    result = ::spng_set_ihdr(m_image_encoder, &ihdr);
    if (result != 0)
    {
        return false;
    }

    m_file_stream = std::make_unique<mango::filesystem::FileStream>(
        path.string(),
        mango::filesystem::FileStream::OpenMode::WRITE
    );

    result = ::spng_encode_image(m_image_encoder, data.data(), data.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    return result != 0;
}

} // namespace erhe::graphics



