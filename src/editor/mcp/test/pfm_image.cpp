#include "pfm_image.hpp"

#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace mcp_test {

auto Pfm_image::read(const std::filesystem::path& path, std::string& error) -> bool
{
    m_width    = 0;
    m_height   = 0;
    m_channels = 0;
    m_values.clear();

    std::ifstream file{path, std::ios::binary};
    if (!file) {
        error = "cannot open " + path.string();
        return false;
    }
    std::string kind;
    int         width {0};
    int         height{0};
    double      scale {0.0};
    file >> kind >> width >> height >> scale;
    if (!file || ((kind != "PF") && (kind != "Pf")) || (width <= 0) || (height <= 0) || (scale == 0.0)) {
        error = "malformed PFM header in " + path.string();
        return false;
    }
    file.get(); // the single whitespace byte that ends the header

    const int         channels    = (kind == "PF") ? 3 : 1;
    const std::size_t value_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels);
    std::vector<float> values(value_count);
    file.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(value_count * sizeof(float)));
    if (!file) {
        error = "truncated PFM data in " + path.string();
        return false;
    }

    // A negative scale marks little-endian data.
    const bool file_little_endian = (scale < 0.0);
    const bool host_little_endian = (std::endian::native == std::endian::little);
    if (file_little_endian != host_little_endian) {
        for (float& value : values) {
            std::uint32_t bits{0};
            std::memcpy(&bits, &value, sizeof(bits));
            bits = ((bits & 0x000000ffu) << 24) | ((bits & 0x0000ff00u) << 8) | ((bits & 0x00ff0000u) >> 8) | ((bits & 0xff000000u) >> 24);
            std::memcpy(&value, &bits, sizeof(bits));
        }
    }

    m_width    = width;
    m_height   = height;
    m_channels = channels;
    m_values   = std::move(values);
    return true;
}

auto Pfm_image::get(const int x, const int y, const int channel) const -> float
{
    const std::size_t index =
        (((static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width)) + static_cast<std::size_t>(x)) * static_cast<std::size_t>(m_channels)) +
        static_cast<std::size_t>(channel);
    return m_values[index];
}

} // namespace mcp_test
