#include "erhe/graphics/png_loader.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/verify.hpp"

#include <mango/mango.hpp>

#include <filesystem>
#include <fstream>

namespace erhe::graphics
{

[[nodiscard]] auto from_mango(const mango::image::Format& format) -> Image_format
{
    using Format = mango::image::Format;

    if (format == Format{24, Format::UNORM, Format::RGB, 8, 8, 8, 0})
    {
        return Image_format::srgb8; // gl::Internal_format::rgb8;
    }

    if (format == Format{32, Format::UNORM, Format::RGBA, 8, 8, 8, 8})
    {
        return Image_format::srgb8_alpha8; //gl::Internal_format::rgba8;
    }

    ERHE_FATAL("unsupported PNG image color type");
}

[[nodiscard]] auto to_mango(const gl::Internal_format format) -> mango::image::Format
{
    using Format = mango::image::Format;

    switch (format)
    {
        //using enum gl::Internal_format;
        case gl::Internal_format::rgb8:  return Format{24, Format::UNORM, Format::RGB,  8, 8, 8, 0};
        case gl::Internal_format::rgba8: return Format{32, Format::UNORM, Format::RGBA, 8, 8, 8, 8};
        default: break;
    }

    ERHE_FATAL("unsupported PNG image color type");
}

[[nodiscard]] auto to_mango(const Image_format format) -> mango::image::Format
{
    using Format = mango::image::Format;

    switch (format)
    {
        //using enum Image_format;
        case Image_format::srgb8:        return Format{24, Format::UNORM, Format::RGB,  8, 8, 8, 0};
        case Image_format::srgb8_alpha8: return Format{32, Format::UNORM, Format::RGBA, 8, 8, 8, 8};
        default: break;
    }

    ERHE_FATAL("unsupported PNG image color type");
}

PNG_loader::PNG_loader() = default;

PNG_loader::~PNG_loader() = default;

void PNG_loader::close()
{
    m_image_decoder.reset();
    m_file.reset();
}

auto PNG_loader::open(
    const std::filesystem::path& path,
    Image_info&     info
) -> bool
{
    m_file = std::make_unique<mango::filesystem::File>(path.string());
    mango::filesystem::File& file = *m_file;
    m_image_decoder = std::make_unique<mango::image::ImageDecoder>(
        file.operator mango::ConstMemory(), ".png"
    );
    if (!m_image_decoder->isDecoder())
    {
        m_file.reset();
        m_image_decoder.reset();
        return false;
    }

    mango::image::ImageHeader header = m_image_decoder->header();
    info.width       = header.width;
    info.height      = header.height;
    info.depth       = header.depth;
    info.level_count = (header.levels > 0) ? header.levels : 1;
    //info.use_mipmaps     = (header.levels > 1);
    info.row_stride  = header.width * header.format.bytes();
    info.format      = from_mango(header.format);

    return true;
}

auto PNG_loader::load(gsl::span<std::byte> transfer_buffer) -> bool
{
    const mango::image::ImageHeader header{m_image_decoder->header()};
    const std::size_t stride = static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.format.bytes());
    mango::image::Surface surface{
        header.width,
        header.height,
        header.format,
        stride,
        transfer_buffer.data()
    };

    const auto status = m_image_decoder->decode(surface);
    return status.success;
}

PNG_writer::PNG_writer() = default;

PNG_writer::~PNG_writer() = default;

auto PNG_writer::write(
    const std::filesystem::path&      path,
    const Image_info&    info,
    gsl::span<std::byte> data
) -> bool
{
    m_image_encoder = std::make_unique<mango::image::ImageEncoder>(".png");
    if (!m_image_encoder->isEncoder())
    {
        m_image_encoder.reset();
        return false;
    }

    mango::image::Surface surface{
        info.width,
        info.height,
        to_mango(info.format),
        static_cast<std::size_t>(info.row_stride),
        data.data()
    };
    m_file_stream = std::make_unique<mango::filesystem::FileStream>(
        path.string(),
        mango::filesystem::FileStream::OpenMode::WRITE
    );
    mango::filesystem::FileStream& file_stream = *m_file_stream;
    mango::image::ImageEncodeOptions encode_options;
    const auto status = m_image_encoder->encode(file_stream, surface, encode_options);
    if (!status.success)
    {
        log_save_png->error("{}", status.info);
        return false;
    }

    return true;
}

} // namespace erhe::graphics



