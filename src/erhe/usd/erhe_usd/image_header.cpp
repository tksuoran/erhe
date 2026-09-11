#include "erhe_usd/image_header.hpp"

#include <array>
#include <fstream>
#include <vector>

namespace erhe::usd {

namespace {

[[nodiscard]] auto read_u16_be(const std::span<const std::uint8_t> bytes, const std::size_t offset) -> std::size_t
{
    return (static_cast<std::size_t>(bytes[offset]) << 8u) | static_cast<std::size_t>(bytes[offset + 1]);
}

[[nodiscard]] auto read_u16_le(const std::span<const std::uint8_t> bytes, const std::size_t offset) -> std::size_t
{
    return (static_cast<std::size_t>(bytes[offset + 1]) << 8u) | static_cast<std::size_t>(bytes[offset]);
}

// PNG: an 8-byte signature, then the IHDR chunk (length, type, width,
// height, bit depth, color type). Color type 0 is gray, 2 is RGB, 3 is a
// palette of 8-bit RGB entries, 4 is gray plus alpha and 6 is RGBA.
[[nodiscard]] auto probe_png(const std::span<const std::uint8_t> bytes) -> Image_header
{
    constexpr std::array<std::uint8_t, 8> signature{0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
    if (bytes.size() < 26) {
        return {};
    }
    for (std::size_t i = 0; i < signature.size(); ++i) {
        if (bytes[i] != signature[i]) {
            return {};
        }
    }
    if ((bytes[12] != 'I') || (bytes[13] != 'H') || (bytes[14] != 'D') || (bytes[15] != 'R')) {
        return {};
    }
    const std::uint8_t bit_depth  = bytes[24];
    const std::uint8_t color_type = bytes[25];
    switch (color_type) {
        case 0: return Image_header{.known = true, .bits_per_component = bit_depth, .component_count = 1};
        case 2: return Image_header{.known = true, .bits_per_component = bit_depth, .component_count = 3};
        case 3: return Image_header{.known = true, .bits_per_component = 8,         .component_count = 3};
        case 4: return Image_header{.known = true, .bits_per_component = bit_depth, .component_count = 2};
        case 6: return Image_header{.known = true, .bits_per_component = bit_depth, .component_count = 4};
        default: return {};
    }
}

// JPEG: an SOI marker, then a sequence of marker segments; the start-of-frame
// segment (SOF0..SOF15, except the DHT / JPG / DAC markers that share the
// range) carries the sample precision and the component count.
[[nodiscard]] auto probe_jpeg(const std::span<const std::uint8_t> bytes) -> Image_header
{
    if ((bytes.size() < 4) || (bytes[0] != 0xffu) || (bytes[1] != 0xd8u)) {
        return {};
    }
    std::size_t position = 2;
    while ((position + 4) <= bytes.size()) {
        if (bytes[position] != 0xffu) {
            return {};
        }
        const std::uint8_t marker = bytes[position + 1];
        if (marker == 0xffu) { // fill byte
            ++position;
            continue;
        }
        const bool standalone = (marker == 0x01u) || ((marker >= 0xd0u) && (marker <= 0xd9u));
        if (standalone) {
            position += 2;
            continue;
        }
        const std::size_t length = read_u16_be(bytes, position + 2);
        const bool is_sof =
            (marker >= 0xc0u) && (marker <= 0xcfu) &&
            (marker != 0xc4u) && (marker != 0xc8u) && (marker != 0xccu);
        if (is_sof) {
            if ((length < 8) || ((position + 2 + 8) > bytes.size())) {
                return {};
            }
            const std::uint8_t precision       = bytes[position + 4];
            const std::uint8_t component_count = bytes[position + 9];
            return Image_header{.known = true, .bits_per_component = precision, .component_count = component_count};
        }
        if (marker == 0xdau) { // start of scan without a frame header before it
            return {};
        }
        position += 2 + length;
    }
    return {};
}

// BMP: "BM", then the file header and the info header whose bit count sits
// at offset 28. Depths up to 8 bits are palette entries of 8-bit RGB.
[[nodiscard]] auto probe_bmp(const std::span<const std::uint8_t> bytes) -> Image_header
{
    if ((bytes.size() < 30) || (bytes[0] != 'B') || (bytes[1] != 'M')) {
        return {};
    }
    const std::size_t bit_count = read_u16_le(bytes, 28);
    switch (bit_count) {
        case 1:
        case 4:
        case 8:  return Image_header{.known = true, .bits_per_component = 8, .component_count = 3};
        case 16: return Image_header{.known = true, .bits_per_component = 5, .component_count = 3};
        case 24: return Image_header{.known = true, .bits_per_component = 8, .component_count = 3};
        case 32: return Image_header{.known = true, .bits_per_component = 8, .component_count = 4};
        default: return {};
    }
}

// TGA has no signature; the image type at byte 2 and the pixel depth at
// byte 16 are checked for the combinations the format defines. Types 1 / 9
// are color-mapped (8-bit RGB entries), 2 / 10 true-color, 3 / 11 gray.
[[nodiscard]] auto probe_tga(const std::span<const std::uint8_t> bytes) -> Image_header
{
    if (bytes.size() < 18) {
        return {};
    }
    const std::uint8_t color_map_type = bytes[1];
    const std::uint8_t image_type     = bytes[2];
    const std::uint8_t pixel_depth    = bytes[16];
    if (color_map_type > 1) {
        return {};
    }
    switch (image_type) {
        case 1:
        case 9:
            if ((color_map_type != 1) || (pixel_depth != 8)) {
                return {};
            }
            return Image_header{.known = true, .bits_per_component = 8, .component_count = 3};
        case 2:
        case 10:
            switch (pixel_depth) {
                case 16: return Image_header{.known = true, .bits_per_component = 5, .component_count = 3};
                case 24: return Image_header{.known = true, .bits_per_component = 8, .component_count = 3};
                case 32: return Image_header{.known = true, .bits_per_component = 8, .component_count = 4};
                default: return {};
            }
        case 3:
        case 11:
            switch (pixel_depth) {
                case 8:  return Image_header{.known = true, .bits_per_component = 8,  .component_count = 1};
                case 16: return Image_header{.known = true, .bits_per_component = 16, .component_count = 1};
                default: return {};
            }
        default:
            return {};
    }
}

[[nodiscard]] auto is_jpeg(const std::span<const std::uint8_t> bytes) -> bool
{
    return (bytes.size() >= 2) && (bytes[0] == 0xffu) && (bytes[1] == 0xd8u);
}

} // anonymous namespace

auto probe_image_header(const std::span<const std::uint8_t> bytes) -> Image_header
{
    // The signatures are distinct, so the first probe that recognizes the
    // bytes owns them; TGA has no signature and is tried last.
    const Image_header png = probe_png(bytes);
    if (png.known) {
        return png;
    }
    if (is_jpeg(bytes)) {
        return probe_jpeg(bytes);
    }
    const Image_header bmp = probe_bmp(bytes);
    if (bmp.known) {
        return bmp;
    }
    return probe_tga(bytes);
}

auto probe_image_header(const std::filesystem::path& path) -> Image_header
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return {};
    }
    constexpr std::size_t     prefix_size = 64 * 1024;
    std::vector<std::uint8_t> bytes(prefix_size);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
    const Image_header header = probe_image_header(bytes);
    if (header.known || (bytes.size() < prefix_size) || !is_jpeg(bytes)) {
        return header;
    }
    // A JPEG whose metadata segments run past the prefix: read the rest.
    stream.clear();
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size <= static_cast<std::streamoff>(bytes.size())) {
        return header;
    }
    bytes.resize(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
    return probe_image_header(bytes);
}

auto is_srgb_by_auto_rule(const Image_header& header) -> bool
{
    return header.known && (header.bits_per_component == 8) && ((header.component_count == 3) || (header.component_count == 4));
}

} // namespace erhe::usd
