#include "erhe_graphics/image_writer_null.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

Image_writer::~Image_writer() noexcept = default;

auto Image_writer_null::write_png(
    const std::filesystem::path& path,
    const int                    width,
    const int                    height,
    const int                    row_stride_bytes,
    const erhe::dataformat::Format format,
    std::span<const std::byte>   data
) -> bool
{
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(row_stride_bytes);
    static_cast<void>(format);
    static_cast<void>(data);
    log_save_png->warn("Image_writer_null: cannot write '{}' (image writer disabled; ERHE_USE_FPNG is off)", path.string());
    return false;
}

auto Image_writer_null::is_supported() const -> bool
{
    return false;
}

auto Image_writer::create() -> std::unique_ptr<Image_writer>
{
    return std::make_unique<Image_writer_null>();
}

} // namespace erhe::graphics
