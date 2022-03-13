#pragma once

#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/filesystem.hpp"

#include <memory>

namespace erhe::graphics
{

enum class Image_format : int
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
    Image_format format     {Image_format::rgba8};
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
        const fs::path& path,
        Image_info&     image_info
    ) -> bool;

    [[nodiscard]] auto load(
        gsl::span<std::byte> transfer_buffer
    ) -> bool;

    void close();
};

} // namespace erhe::graphics
