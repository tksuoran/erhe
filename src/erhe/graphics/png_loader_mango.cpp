#include "erhe/graphics/png_loader.hpp"
#include "erhe/graphics/log.hpp"
#include <fstream>

namespace erhe::graphics
{

using Format = mango::Format;

auto from_mango(const mango::Format& format)
-> gl::Internal_format
{
    if (format == mango::Format(24, mango::Format::UNORM, mango::Format::RGB,  8, 8, 8, 0))
    {
        return gl::Internal_format::rgb8;
    }

    if (format == mango::Format(32, mango::Format::UNORM, mango::Format::RGBA, 8, 8, 8, 8))
    {
        return gl::Internal_format::rgba8;
    }

    FATAL("unsupported PNG image color type");
}

auto to_mango(gl::Internal_format format)
-> mango::Format
{
    switch (format)
    {
        case gl::Internal_format::rgb8:  return mango::Format(24, mango::Format::UNORM, mango::Format::RGB,  8, 8, 8, 0);
        case gl::Internal_format::rgba8: return mango::Format(32, mango::Format::UNORM, mango::Format::RGBA, 8, 8, 8, 8);
        default: break;
    }

    FATAL("unsupported PNG image color type");
}

PNG_loader::~PNG_loader()
{
    close();
}

void PNG_loader::close()
{
    m_image_decoder.reset();
    m_file.reset();
}

auto PNG_loader::open(const std::filesystem::path& path,
                      Texture::Create_info&        create_info)
-> bool
{
    m_file = std::make_unique<mango::filesystem::File>(path.string());
    mango::filesystem::File& file = *m_file;
    m_image_decoder = std::make_unique<mango::ImageDecoder>(file.operator mango::ConstMemory(), ".png");
    if (!m_image_decoder->isDecoder())
    {
        m_file.reset();
        m_image_decoder.reset();
        return false;
    }

    mango::ImageHeader header = m_image_decoder->header();

    create_info.width           = header.width;
    create_info.height          = header.height;
    create_info.depth           = header.depth;
    create_info.level_count     = (header.levels > 0) ? header.levels : 1;
    create_info.use_mipmaps     = (header.levels > 1);
    create_info.row_stride      = header.width * header.format.bytes();
    create_info.internal_format = from_mango(header.format);

    return true;
}

auto PNG_loader::load(gsl::span<std::byte> transfer_buffer)
-> bool
{
    mango::ImageHeader header = m_image_decoder->header();
    auto stride = header.width * header.format.bytes();
    mango::Surface surface(header.width, header.height, header.format, stride, transfer_buffer.data());

    auto status = m_image_decoder->decode(surface);
    return status.success;
}


auto PNG_writer::write(const std::filesystem::path& path,
                       gl::Internal_format          internal_format,
                       int                          width,
                       int                          height,
                       int                          row_stride,
                       gsl::span<std::byte>         data)
-> bool
{
    m_image_encoder = std::make_unique<mango::ImageEncoder>(".png");
    if (!m_image_encoder->isEncoder())
    {
        m_image_encoder.reset();
        return false;
    }
    mango::Surface surface(width, height, to_mango(internal_format), row_stride, data.data());
    m_file_stream = std::make_unique<mango::filesystem::FileStream>(path.string(),
                                                                    mango::filesystem::FileStream::OpenMode::WRITE);
    mango::filesystem::FileStream& file_stream = *m_file_stream;
    mango::ImageEncodeOptions encode_options;
    auto status = m_image_encoder->encode(file_stream, surface, encode_options);
    if (!status.success)
    {
        log_save_png.error("{}", status.info);
        return false;
    }

    return true;
}

} // namespace erhe::graphics



