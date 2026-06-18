#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

namespace erhe::graphics {

// Abstraction over saving CPU-side pixel data to an image file.
//
// The backend is selected at build time:
//   - Image_writer_fpng -- saves PNG via fpng (a fast PNG encoder). Available
//                          whenever ERHE_USE_FPNG is defined. fpng has no
//                          external dependencies and is independent of the
//                          window library, so headless builds can save too.
//   - Image_writer_null -- always fails; used when ERHE_USE_FPNG is off.
//
// Create the active backend with Image_writer::create().
class Image_writer
{
public:
    virtual ~Image_writer() noexcept;

    // Saves pixel data to a PNG file at path. Rows are row_stride_bytes apart
    // (pass width * bytes_per_pixel for tightly packed data). format describes
    // the pixel layout; only 8-bit RGBA and 8-bit RGB are supported. Returns
    // false on error or when the null backend is active.
    [[nodiscard]] virtual auto write_png(
        const std::filesystem::path& path,
        int                          width,
        int                          height,
        int                          row_stride_bytes,
        erhe::dataformat::Format     format,
        std::span<const std::byte>   data
    ) -> bool = 0;

    // True when a real (non-null) backend is active.
    [[nodiscard]] virtual auto is_supported() const -> bool = 0;

    // Returns the backend selected at build time. Defined by whichever backend
    // translation unit is compiled (image_writer_sdl.cpp or image_writer_null.cpp).
    [[nodiscard]] static auto create() -> std::unique_ptr<Image_writer>;
};

} // namespace erhe::graphics
