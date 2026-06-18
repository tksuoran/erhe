#pragma once

#include "erhe_graphics/image_writer.hpp"

namespace erhe::graphics {

// fpng image writer: saves PNG via fpng (a fast PNG encoder). Compiled when
// ERHE_USE_FPNG is defined (independent of the window library).
class Image_writer_fpng final : public Image_writer
{
public:
    [[nodiscard]] auto write_png(
        const std::filesystem::path& path,
        int                          width,
        int                          height,
        int                          row_stride_bytes,
        erhe::dataformat::Format     format,
        std::span<const std::byte>   data
    ) -> bool override;

    [[nodiscard]] auto is_supported() const -> bool override;
};

} // namespace erhe::graphics
