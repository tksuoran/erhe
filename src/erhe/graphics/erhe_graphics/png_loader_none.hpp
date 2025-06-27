#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/texture.hpp"

#include <filesystem>
#include <memory>

namespace erhe::graphics {

class Image_info
{
public:
    int                      width      {0};
    int                      height     {0};
    int                      depth      {0};
    int                      level_count{0};
    int                      row_stride {0};
    erhe::dataformat::Format format     {erhe::dataformat::Format::format_8_vec4_srgb};
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

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info) -> bool;
    [[nodiscard]] auto load(std::span<std::byte> transfer_buffer) -> bool;
    void close();
};

} // namespace erhe::graphics
