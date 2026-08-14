#include "erhe_graphics/image_loader_dds.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_profile/profile.hpp"

#include <dds.hpp>

#include <cstring>
#include <fstream>
#include <vector>

namespace erhe::graphics {

namespace {

constexpr std::uint8_t dds_magic[4] = { 'D', 'D', 'S', ' ' };

// Legacy (non-DX10) DDS headers carry no color space information and map to
// the plain UNORM DXGI formats; DX10 headers are authoritative.
auto dxgi_to_erhe(const DXGI_FORMAT dxgi_format, const bool supports_alpha) -> erhe::dataformat::Format
{
    switch (dxgi_format) {
        case DXGI_FORMAT_BC1_UNORM:          return supports_alpha ? erhe::dataformat::Format::format_bc1_rgba_unorm : erhe::dataformat::Format::format_bc1_rgb_unorm;
        case DXGI_FORMAT_BC1_UNORM_SRGB:     return supports_alpha ? erhe::dataformat::Format::format_bc1_rgba_srgb  : erhe::dataformat::Format::format_bc1_rgb_srgb;
        case DXGI_FORMAT_BC2_UNORM:          return erhe::dataformat::Format::format_bc2_unorm;
        case DXGI_FORMAT_BC2_UNORM_SRGB:     return erhe::dataformat::Format::format_bc2_srgb;
        case DXGI_FORMAT_BC3_UNORM:          return erhe::dataformat::Format::format_bc3_unorm;
        case DXGI_FORMAT_BC3_UNORM_SRGB:     return erhe::dataformat::Format::format_bc3_srgb;
        case DXGI_FORMAT_BC4_UNORM:          return erhe::dataformat::Format::format_bc4_unorm;
        case DXGI_FORMAT_BC4_SNORM:          return erhe::dataformat::Format::format_bc4_snorm;
        case DXGI_FORMAT_BC5_UNORM:          return erhe::dataformat::Format::format_bc5_unorm;
        case DXGI_FORMAT_BC5_SNORM:          return erhe::dataformat::Format::format_bc5_snorm;
        case DXGI_FORMAT_BC6H_UF16:          return erhe::dataformat::Format::format_bc6h_ufloat;
        case DXGI_FORMAT_BC6H_SF16:          return erhe::dataformat::Format::format_bc6h_sfloat;
        case DXGI_FORMAT_BC7_UNORM:          return erhe::dataformat::Format::format_bc7_unorm;
        case DXGI_FORMAT_BC7_UNORM_SRGB:     return erhe::dataformat::Format::format_bc7_srgb;
        case DXGI_FORMAT_R8G8B8A8_UNORM:     return erhe::dataformat::Format::format_8_vec4_unorm;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return erhe::dataformat::Format::format_8_vec4_srgb;
        case DXGI_FORMAT_B8G8R8A8_UNORM:     return erhe::dataformat::Format::format_8_vec4_bgra_unorm;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return erhe::dataformat::Format::format_8_vec4_bgra_srgb;
        case DXGI_FORMAT_R8_UNORM:           return erhe::dataformat::Format::format_8_scalar_unorm;
        case DXGI_FORMAT_R8G8_UNORM:         return erhe::dataformat::Format::format_8_vec2_unorm;
        default:                             return erhe::dataformat::Format::format_undefined;
    }
}

} // anonymous namespace

class Image_loader_dds_impl_state
{
public:
    dds::Image                image;
    std::vector<std::uint8_t> owned_data;   // backing storage for the path-based open()
    Image_info                info;
    bool                      is_open{false};
};

Image_loader_dds::Image_loader_dds()
    : m_state{std::make_unique<Image_loader_dds_impl_state>()}
{
}

Image_loader_dds::~Image_loader_dds() noexcept = default;

auto Image_loader_dds::is_dds(const std::span<const std::uint8_t>& buffer_view) -> bool
{
    if (buffer_view.size() < sizeof(dds_magic)) {
        return false;
    }
    return std::memcmp(buffer_view.data(), dds_magic, sizeof(dds_magic)) == 0;
}

auto Image_loader_dds::open(const std::filesystem::path& path, Image_info& image_info, const bool linear) -> bool
{
    ERHE_PROFILE_FUNCTION();

    close();

    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) {
        log_texture->warn("DDS: cannot open file '{}'", path.string());
        return false;
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        log_texture->warn("DDS: empty file '{}'", path.string());
        return false;
    }
    stream.seekg(0, std::ios::beg);
    m_state->owned_data.resize(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(m_state->owned_data.data()), size)) {
        log_texture->warn("DDS: read failed for '{}'", path.string());
        m_state->owned_data.clear();
        return false;
    }
    const std::span<const std::uint8_t> buffer_view{m_state->owned_data.data(), m_state->owned_data.size()};
    return open(buffer_view, image_info, linear);
}

auto Image_loader_dds::open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info, const bool linear) -> bool
{
    ERHE_PROFILE_FUNCTION();

    // Note: unlike the file-based open(), this open() does not close()
    // first, because the file-based open() forwards here after filling
    // owned_data - and close() would discard it.
    static_cast<void>(linear); // the DXGI format in the file is authoritative for color space

    m_state->is_open = false;
    m_state->image   = dds::Image{};
    const dds::ReadResult read_result = dds::readImage(buffer_view.data(), buffer_view.size(), &m_state->image);
    if (read_result != dds::ReadResult::Success) {
        log_texture->warn("DDS: container parse failed (result {})", static_cast<int>(read_result));
        return false;
    }
    const dds::Image& image = m_state->image;
    const erhe::dataformat::Format format = dxgi_to_erhe(image.format, image.supportsAlpha);
    if (format == erhe::dataformat::Format::format_undefined) {
        log_texture->warn("DDS: unsupported DXGI format {}", static_cast<int>(image.format));
        return false;
    }
    if ((image.dimension == dds::Texture3D) || (image.depth > 1) || (image.arraySize > 1)) {
        log_texture->warn("DDS: 3D and array images are not supported");
        return false;
    }
    if (image.mipmaps.empty()) {
        log_texture->warn("DDS: no image data");
        return false;
    }

    const std::size_t block_extent    = erhe::dataformat::get_block_extent(format);
    const std::size_t block_count_x   = (static_cast<std::size_t>(image.width) + block_extent - 1) / block_extent;
    const std::size_t level0_row_size = block_count_x * erhe::dataformat::get_format_size_bytes(format);

    m_state->info = Image_info{
        .width       = static_cast<int>(image.width),
        .height      = static_cast<int>(image.height),
        .depth       = 1,
        .level_count = static_cast<int>(image.mipmaps.size()),
        .row_stride  = static_cast<int>(level0_row_size),
        .format      = format
    };
    image_info       = m_state->info;
    m_state->is_open = true;
    return true;
}

auto Image_loader_dds::load(std::span<std::uint8_t> transfer_buffer) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_state->is_open) {
        return false;
    }
    const dds::Image& image = m_state->image;
    std::size_t total_byte_count = 0;
    for (const auto& mip : image.mipmaps) {
        total_byte_count += mip.size();
    }
    if (transfer_buffer.size() < total_byte_count) {
        log_texture->warn("DDS: transfer buffer too small: {} < {}", transfer_buffer.size(), total_byte_count);
        return false;
    }
    std::size_t write_offset = 0;
    for (const auto& mip : image.mipmaps) {
        std::memcpy(transfer_buffer.data() + write_offset, mip.data(), mip.size());
        write_offset += mip.size();
    }
    return true;
}

void Image_loader_dds::close()
{
    m_state->is_open = false;
    m_state->image   = dds::Image{};
    m_state->owned_data.clear();
}

} // namespace erhe::graphics
