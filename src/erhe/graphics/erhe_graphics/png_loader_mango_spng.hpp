#pragma once

#include <span>

#include <filesystem>
#include <memory>

extern "C"
{
    struct spng_ctx;
};

namespace mango {
    namespace filesystem {
        class File;
        class FileStream;
    }
}

namespace erhe::graphics {

enum class Image_format : int {
    srgb8 = 0,
    srgb8_alpha8,
};

class Image_info
{
public:
    int          width      {0};
    int          height     {0};
    int          depth      {0};
    int          level_count{0};
    int          row_stride {0};
    Image_format format     {Image_format::srgb8_alpha8};
};

class PNG_loader final
{
public:
    PNG_loader    ();
    ~PNG_loader   () noexcept;
    PNG_loader    (const PNG_loader&) = delete;
    void operator=(const PNG_loader&) = delete;
    PNG_loader    (PNG_loader&&)      = delete;
    void operator=(PNG_loader&&)      = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info) -> bool;
    [[nodiscard]] auto open(
        const std::span<const std::byte>& png_encoded_buffer_view,
        Image_info&                       image_info
    ) -> bool;

    [[nodiscard]] auto load(std::span<std::byte> transfer_buffer) -> bool;

    void close();

private:
    std::unique_ptr<mango::filesystem::File> m_file;
    struct ::spng_ctx*                       m_image_decoder{nullptr};
    //std::unique_ptr<mango::image::ImageDecoder> m_image_decoder;
};

class PNG_writer final
{
public:
    PNG_writer    ();
    ~PNG_writer   () noexcept;
    PNG_writer    (const PNG_writer&) = delete;
    PNG_writer    (PNG_writer&&)      = delete;
    auto operator=(const PNG_writer&) = delete;
    auto operator=(PNG_writer&&)      = delete;

    [[nodiscard]] auto write(
        const std::filesystem::path& path,
        const Image_info&            info,
        std::span<std::byte>         data
    ) -> bool;

    auto stream_op(void* dst_src, std::size_t length) -> int;

private:
    std::unique_ptr<mango::filesystem::FileStream> m_file_stream;
    struct ::spng_ctx*                             m_image_encoder{nullptr};
    //std::unique_ptr<mango::image::ImageEncoder>    m_image_encoder;
};

} // namespace erhe::graphics
