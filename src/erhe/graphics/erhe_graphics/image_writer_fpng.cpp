#include "erhe_graphics/image_writer_fpng.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include "fpng.h"

#include <cstring>
#include <vector>

namespace erhe::graphics {

Image_writer::~Image_writer() noexcept = default;

namespace {

// fpng_init() must run once before any encode. Thread-safe static
// initialization guarantees a single call.
void ensure_fpng_initialized()
{
    static const bool initialized = [] {
        fpng::fpng_init();
        return true;
    }();
    static_cast<void>(initialized);
}

} // namespace

auto Image_writer_fpng::write_png(
    const std::filesystem::path& path,
    const int                    width,
    const int                    height,
    const int                    row_stride_bytes,
    const erhe::dataformat::Format format,
    std::span<const std::byte>   data
) -> bool
{
    const std::size_t component_count   = erhe::dataformat::get_component_count(format);
    const std::size_t bytes_per_channel = erhe::dataformat::get_component_byte_size(format);
    if ((bytes_per_channel != 1) || ((component_count != 3) && (component_count != 4))) {
        log_save_png->error("Image_writer_fpng: unsupported pixel format for '{}' (need 8-bit RGB or RGBA)", path.string());
        return false;
    }
    if ((width <= 0) || (height <= 0)) {
        log_save_png->error("Image_writer_fpng: invalid size {}x{} for '{}'", width, height, path.string());
        return false;
    }

    const std::size_t num_chans   = component_count;
    const std::size_t tight_pitch = static_cast<std::size_t>(width) * num_chans;
    const std::size_t required    = static_cast<std::size_t>(row_stride_bytes) * static_cast<std::size_t>(height);
    if (data.size() < required) {
        log_save_png->error(
            "Image_writer_fpng: data too small ({} < {}) for '{}'",
            data.size(), required, path.string()
        );
        return false;
    }

    // fpng expects tightly packed rows (pitch == width * num_chans). Repack
    // when the source is strided.
    const void* image_ptr = data.data();
    std::vector<std::byte> packed;
    if (static_cast<std::size_t>(row_stride_bytes) != tight_pitch) {
        packed.resize(tight_pitch * static_cast<std::size_t>(height));
        for (int y = 0; y < height; ++y) {
            const std::byte* const src = data.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(row_stride_bytes);
            std::byte* const       dst = packed.data() + static_cast<std::size_t>(y) * tight_pitch;
            std::memcpy(dst, src, tight_pitch);
        }
        image_ptr = packed.data();
    }

    ensure_fpng_initialized();

    const bool ok = fpng::fpng_encode_image_to_file(
        path.string().c_str(),
        image_ptr,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        static_cast<std::uint32_t>(num_chans),
        0
    );
    if (!ok) {
        log_save_png->error("Image_writer_fpng: fpng_encode_image_to_file failed for '{}'", path.string());
        return false;
    }

    log_save_png->info("Saved PNG '{}' ({}x{}, {} channels)", path.string(), width, height, num_chans);
    return true;
}

auto Image_writer_fpng::is_supported() const -> bool
{
    return true;
}

auto Image_writer::create() -> std::unique_ptr<Image_writer>
{
    return std::make_unique<Image_writer_fpng>();
}

} // namespace erhe::graphics
