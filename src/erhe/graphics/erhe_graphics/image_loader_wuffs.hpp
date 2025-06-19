#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace mango {
    namespace filesystem {
        class File;
        class FileStream;
    }
}

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

class Image_loader_impl;

class Image_loader
{
public:
    Image_loader  ();
    ~Image_loader () noexcept;
    Image_loader  (const Image_loader&) = delete;
    auto operator=(const Image_loader&) = delete;
    Image_loader  (Image_loader&&)      = delete;
    auto operator=(Image_loader&&)      = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info) -> bool;
    [[nodiscard]] auto open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info) -> bool;
    [[nodiscard]] auto load(std::span<std::uint8_t> transfer_buffer) -> bool;
    void close();

private:
    std::unique_ptr<Image_loader_impl> m_impl;
};

} // namespace erhe::graphics
