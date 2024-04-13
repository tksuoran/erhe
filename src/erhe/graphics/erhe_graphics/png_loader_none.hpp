#pragma once

#include <filesystem>
#include <memory>
#include <span>

namespace erhe::graphics
{

enum class Image_format : int
{
    srgb8 = 0,
    srgb8_alpha8,
};

class Image_info
{
public:
    size_t       width      {0};
    size_t       height     {0};
    size_t       depth      {0};
    size_t       level_count{0};
    size_t       row_stride {0};
    Image_format format     {Image_format::srgb8_alpha8};
};

class PNG_loader
{
public:
    PNG_loader    ();
    ~PNG_loader   () noexcept;
    PNG_loader    (const PNG_loader&) = delete;
    auto operator=(const PNG_loader&) = delete;
    PNG_loader    (PNG_loader&&)      = delete;
    auto operator=(PNG_loader&&)      = delete;

    [[nodiscard]] auto open(
        const std::filesystem::path& path,
        Image_info&                  image_info
    ) -> bool;

    [[nodiscard]] auto open(
        const std::span<const std::byte>& png_encoded_buffer_view,
        Image_info&                       image_info
    ) -> bool;

    [[nodiscard]] auto load(
        std::span<std::byte> transfer_buffer
    ) -> bool;

    void close();
};

} // namespace erhe::graphics
