#pragma once

#include "erhe_graphics/image_writer.hpp"

namespace erhe::graphics {

// Null image writer: always fails. Compiled when the fpng backend is disabled
// (ERHE_USE_FPNG not defined).
class Image_writer_null final : public Image_writer
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
