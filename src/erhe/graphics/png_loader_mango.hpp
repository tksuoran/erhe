#ifndef png_loader_mango_hpp_erhe_toolkit
#define png_loader_mango_hpp_erhe_toolkit

#include "erhe/graphics/texture.hpp"
#include <mango/mango.hpp>
#include <filesystem>
#include <memory>

namespace erhe::graphics
{

class PNG_loader
{
public:
    PNG_loader() = default;

    PNG_loader(const PNG_loader&) = delete;

    auto operator=(const PNG_loader&)
    -> PNG_loader& = delete;

    PNG_loader(PNG_loader&&) = delete;

    auto operator=(PNG_loader&&)
    -> PNG_loader = delete;

    ~PNG_loader();

    auto open(const std::filesystem::path& path,
              Texture::Create_info&        create_info)
    -> bool;

    auto load(gsl::span<std::byte> transfer_buffer)
    -> bool;

    void close();

private:
    std::unique_ptr<mango::filesystem::File> m_file;
    std::unique_ptr<mango::ImageDecoder>     m_image_decoder;
};

class PNG_writer
{
public:
    PNG_writer() = default;

    PNG_writer(const PNG_writer&) = delete;

    auto operator=(const PNG_writer&)
    -> PNG_writer& = delete;

    PNG_writer(PNG_writer&&) = delete;

    auto operator=(PNG_writer&&)
    -> PNG_writer = delete;

    ~PNG_writer();

    auto write(const std::filesystem::path& path,
               gl::Internal_format          internal_format,
               int                          width,
               int                          height,
               int                          row_stride,
               gsl::span<std::byte>         data)
    -> bool;


private:
    std::unique_ptr<mango::filesystem::FileStream> m_file_stream;
    std::unique_ptr<mango::ImageEncoder>           m_image_encoder;
};

} // namespace erhe::graphics

#endif

