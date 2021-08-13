#pragma once

#include <filesystem>
#include <memory>
#include <gsl/span>

namespace mango {
    namespace filesystem {
        class File;
        class FileStream;
    }
    class ImageEncoder;
    class ImageDecoder;
}

namespace erhe::graphics
{

enum Image_format
{
    rgb8 = 0,
    rgba8,
};

class Image_info
{
public:
    int          width      {0};
    int          height     {0};
    int          depth      {0};
    int          level_count{0};
    int          row_stride {0};
    Image_format format     {rgba8};
};

class PNG_loader final
{
public:
    PNG_loader    ();
    ~PNG_loader   ();
    PNG_loader    (const PNG_loader&) = delete;
    void operator=(const PNG_loader&) = delete;
    PNG_loader    (PNG_loader&&)      = delete;
    void operator=(PNG_loader&&)      = delete;

    auto open(const std::filesystem::path& path, Image_info& image_info) -> bool;

    auto load(gsl::span<std::byte> transfer_buffer) -> bool;

    void close();

private:
    std::unique_ptr<mango::filesystem::File> m_file;
    std::unique_ptr<mango::ImageDecoder>     m_image_decoder;
};

class PNG_writer final
{
public:
    PNG_writer    ();
    ~PNG_writer   ();
    PNG_writer    (const PNG_writer&) = delete;
    PNG_writer    (PNG_writer&&)      = delete;
    auto operator=(const PNG_writer&) = delete;
    auto operator=(PNG_writer&&)      = delete;

    auto write(const std::filesystem::path& path,
               const Image_info&            info,
               gsl::span<std::byte>         data)
    -> bool;

private:
    std::unique_ptr<mango::filesystem::FileStream> m_file_stream;
    std::unique_ptr<mango::ImageEncoder>           m_image_encoder;
};

} // namespace erhe::graphics
