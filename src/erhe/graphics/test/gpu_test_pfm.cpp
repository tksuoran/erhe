#include "gpu_test_pfm.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace erhe::graphics::test {

namespace {

// Reads one whitespace-delimited header token. PFM headers are ASCII tokens
// separated by whitespace; the single whitespace byte after the scale token
// ends the header.
[[nodiscard]] auto read_token(std::ifstream& stream, std::string& token) -> bool
{
    token.clear();
    char c = 0;
    while (stream.get(c)) {
        if ((c != ' ') && (c != '\t') && (c != '\n') && (c != '\r')) {
            token.push_back(c);
            break;
        }
    }
    if (token.empty()) {
        return false;
    }
    while (stream.get(c)) {
        if ((c == ' ') || (c == '\t') || (c == '\n') || (c == '\r')) {
            return true;
        }
        token.push_back(c);
    }
    return true;
}

[[nodiscard]] auto swap_float_bytes(const float value) -> float
{
    const uint32_t bits    = std::bit_cast<uint32_t>(value);
    const uint32_t swapped =
        ((bits & 0x000000ffu) << 24u) |
        ((bits & 0x0000ff00u) <<  8u) |
        ((bits & 0x00ff0000u) >>  8u) |
        ((bits & 0xff000000u) >> 24u);
    return std::bit_cast<float>(swapped);
}

} // anonymous namespace

auto read_pfm(const std::filesystem::path& path, Pfm_image& image) -> bool
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return false;
    }
    std::string magic;
    std::string width_token;
    std::string height_token;
    std::string scale_token;
    if (
        !read_token(stream, magic)        ||
        !read_token(stream, width_token)  ||
        !read_token(stream, height_token) ||
        !read_token(stream, scale_token)
    ) {
        return false;
    }
    if (magic != "PF") {
        return false;
    }
    const int   width  = std::stoi(width_token);
    const int   height = std::stoi(height_token);
    const float scale  = std::stof(scale_token);
    if ((width <= 0) || (height <= 0)) {
        return false;
    }

    const bool file_is_little_endian = (scale < 0.0f);
    const bool host_is_little_endian = (std::endian::native == std::endian::little);
    const bool swap_bytes            = (file_is_little_endian != host_is_little_endian);

    const std::size_t row_floats = static_cast<std::size_t>(width) * 3u;
    image.width  = width;
    image.height = height;
    image.rgb.resize(row_floats * static_cast<std::size_t>(height));
    std::vector<float> row(row_floats);
    for (int file_row = 0; file_row < height; ++file_row) {
        stream.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row_floats * sizeof(float)));
        if (!stream) {
            return false;
        }
        if (swap_bytes) {
            for (float& value : row) {
                value = swap_float_bytes(value);
            }
        }
        // File row 0 is the bottom row of the image.
        const std::size_t image_row = static_cast<std::size_t>(height - 1 - file_row);
        std::memcpy(image.rgb.data() + (image_row * row_floats), row.data(), row_floats * sizeof(float));
    }
    return true;
}

auto write_pfm(const std::filesystem::path& path, const Pfm_image& image) -> bool
{
    const std::size_t row_floats = static_cast<std::size_t>(image.width) * 3u;
    if ((image.width <= 0) || (image.height <= 0) || (image.rgb.size() != (row_floats * static_cast<std::size_t>(image.height)))) {
        return false;
    }
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        return false;
    }
    const bool        host_is_little_endian = (std::endian::native == std::endian::little);
    const std::string header =
        "PF\n" + std::to_string(image.width) + " " + std::to_string(image.height) + "\n" +
        (host_is_little_endian ? "-1.0\n" : "1.0\n");
    stream.write(header.data(), static_cast<std::streamsize>(header.size()));
    for (int file_row = 0; file_row < image.height; ++file_row) {
        const std::size_t image_row = static_cast<std::size_t>(image.height - 1 - file_row);
        stream.write(
            reinterpret_cast<const char*>(image.rgb.data() + (image_row * row_floats)),
            static_cast<std::streamsize>(row_floats * sizeof(float))
        );
    }
    return static_cast<bool>(stream);
}

} // namespace erhe::graphics::test
