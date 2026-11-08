#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/image_loader.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace {

// Counts log_texture warnings (level lowered to warn) while alive.
class Image_warning_sink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    std::size_t count{0};

protected:
    void sink_it_(const spdlog::details::log_msg& message) override
    {
        if (message.level >= spdlog::level::warn) {
            ++count;
        }
    }
    void flush_() override {}
};

class Scoped_image_warning_counter
{
public:
    Scoped_image_warning_counter()
        : m_sink {std::make_shared<Image_warning_sink>()}
        , m_level{erhe::graphics::log_texture->level()}
    {
        erhe::graphics::log_texture->sinks().push_back(m_sink);
        erhe::graphics::log_texture->set_level(spdlog::level::warn);
    }
    ~Scoped_image_warning_counter() noexcept
    {
        erhe::graphics::log_texture->set_level(m_level);
        std::vector<spdlog::sink_ptr>& sinks = erhe::graphics::log_texture->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }
    Scoped_image_warning_counter(const Scoped_image_warning_counter&) = delete;
    auto operator=(const Scoped_image_warning_counter&) = delete;
    [[nodiscard]] auto get_count() const -> std::size_t { return m_sink->count; }

private:
    std::shared_ptr<Image_warning_sink> m_sink;
    spdlog::level::level_enum           m_level;
};

auto crc32(std::span<const std::uint8_t> bytes) -> std::uint32_t
{
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = ((crc & 1u) != 0) ? ((crc >> 1) ^ 0xedb88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xffffffffu;
}

void put_u32_be(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >>  8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>( value        & 0xffu));
}

void put_signature(std::vector<std::uint8_t>& out)
{
    const std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    out.insert(out.end(), signature.begin(), signature.end());
}

void put_chunk(std::vector<std::uint8_t>& out, const std::string_view type, std::span<const std::uint8_t> data, const std::uint32_t crc_xor = 0)
{
    put_u32_be(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> crc_bytes{type.begin(), type.end()};
    crc_bytes.insert(crc_bytes.end(), data.begin(), data.end());
    out.insert(out.end(), crc_bytes.begin(), crc_bytes.end());
    put_u32_be(out, crc32(crc_bytes) ^ crc_xor);
}

auto make_ihdr(const std::uint32_t width, const std::uint32_t height) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> ihdr;
    put_u32_be(ihdr, width);
    put_u32_be(ihdr, height);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // color type RGBA
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    return ihdr;
}

// zlib stream of one stored deflate block holding the 2x2 RGBA scanlines.
auto make_idat_2x2() -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < 2; ++y) {
        raw.push_back(0); // filter none
        for (int x = 0; x < 2; ++x) {
            raw.push_back(static_cast<std::uint8_t>(64 * x));
            raw.push_back(static_cast<std::uint8_t>(64 * y));
            raw.push_back(128);
            raw.push_back(255);
        }
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01, 0x01};
    const std::uint16_t length = static_cast<std::uint16_t>(raw.size());
    zlib.push_back(static_cast<std::uint8_t>(length & 0xffu));
    zlib.push_back(static_cast<std::uint8_t>(length >> 8));
    zlib.push_back(static_cast<std::uint8_t>(~length & 0xffu));
    zlib.push_back(static_cast<std::uint8_t>((~length >> 8) & 0xffu));
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    put_u32_be(zlib, (b << 16) | a);
    return zlib;
}

auto make_png(const std::uint32_t width, const std::uint32_t height, const std::size_t text_byte_count = 0) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    put_chunk(png, "IHDR", make_ihdr(width, height));
    if (text_byte_count > 0) {
        std::vector<std::uint8_t> text{'C', 'o', 'm', 'm', 'e', 'n', 't', 0};
        text.resize(text_byte_count, 'x');
        put_chunk(png, "tEXt", text);
    }
    put_chunk(png, "IDAT", make_idat_2x2());
    put_chunk(png, "IEND", {});
    return png;
}

// Open and, when that succeeds, load; true when the whole decode succeeds.
auto decode(std::span<const std::uint8_t> bytes) -> bool
{
    erhe::graphics::Image_loader loader;
    erhe::graphics::Image_info   info{};
    if (!loader.open(bytes, info, false)) {
        return false;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(info.row_stride) * static_cast<std::size_t>(info.height));
    const bool loaded = loader.load(pixels);
    loader.close();
    return loaded;
}

auto probe(std::span<const std::uint8_t> bytes, erhe::graphics::Image_info& info) -> erhe::graphics::Image_prefix_result
{
    erhe::graphics::Image_loader loader;
    return loader.probe_prefix(bytes, info, false);
}

} // namespace

TEST(Image_loader_malformed, valid_png_decodes)
{
    const Scoped_image_warning_counter warnings;
    EXPECT_TRUE(decode(make_png(2, 2)));
    EXPECT_EQ(warnings.get_count(), 0u);
}

TEST(Image_loader_malformed, malformed_png_warns_once)
{
    const std::vector<std::uint8_t> png = make_png(100000, 100000);
    const Scoped_image_warning_counter warnings;
    EXPECT_FALSE(decode(png));
    EXPECT_EQ(warnings.get_count(), 1u);
}

TEST(Image_prefix_probe, header_past_the_prefix_needs_more_without_a_warning)
{
    const std::vector<std::uint8_t> png = make_png(2, 2, 70 * 1024);
    erhe::graphics::Image_info info{};
    {
        const Scoped_image_warning_counter warnings;
        EXPECT_EQ(probe(std::span<const std::uint8_t>{png.data(), 65536}, info), erhe::graphics::Image_prefix_result::needs_more);
        EXPECT_EQ(warnings.get_count(), 0u);
    }
    const Scoped_image_warning_counter warnings;
    EXPECT_EQ(probe(png, info), erhe::graphics::Image_prefix_result::complete);
    EXPECT_EQ(info.width, 2);
    EXPECT_EQ(info.height, 2);
    EXPECT_TRUE(decode(png));
    EXPECT_EQ(warnings.get_count(), 0u);
}

TEST(Image_prefix_probe, header_inside_the_prefix_is_complete)
{
    const std::vector<std::uint8_t> png = make_png(2, 2);
    erhe::graphics::Image_info info{};
    const Scoped_image_warning_counter warnings;
    // The PNG header runs to the first IDAT: here the prefix ends before IEND.
    EXPECT_EQ(probe(std::span<const std::uint8_t>{png.data(), png.size() - 12}, info), erhe::graphics::Image_prefix_result::complete);
    EXPECT_EQ(info.width, 2);
    EXPECT_EQ(warnings.get_count(), 0u);
}

TEST(Image_prefix_probe, short_prefix_needs_more)
{
    const std::vector<std::uint8_t> png = make_png(2, 2);
    erhe::graphics::Image_info info{};
    const Scoped_image_warning_counter warnings;
    EXPECT_EQ(probe(std::span<const std::uint8_t>{png.data(), 4}, info), erhe::graphics::Image_prefix_result::needs_more);
    EXPECT_EQ(probe(std::span<const std::uint8_t>{png.data(), 20}, info), erhe::graphics::Image_prefix_result::needs_more);
    EXPECT_EQ(warnings.get_count(), 0u);
}

TEST(Image_prefix_probe, malformed_header_warns_once)
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    put_chunk(png, "IHDR", make_ihdr(2, 2), 0x5a5a5a5au);
    erhe::graphics::Image_info info{};
    const Scoped_image_warning_counter warnings;
    EXPECT_EQ(probe(png, info), erhe::graphics::Image_prefix_result::malformed);
    EXPECT_EQ(warnings.get_count(), 1u);
}

TEST(Image_prefix_probe, unknown_format_is_malformed)
{
    const std::array<std::uint8_t, 16> bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    erhe::graphics::Image_info info{};
    EXPECT_EQ(probe(bytes, info), erhe::graphics::Image_prefix_result::malformed);
}

TEST(Image_loader_malformed, truncated_after_ihdr)
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    put_chunk(png, "IHDR", make_ihdr(2, 2));
    EXPECT_FALSE(decode(png));
}

TEST(Image_loader_malformed, truncated_inside_ihdr)
{
    const std::vector<std::uint8_t> png = make_png(2, 2);
    EXPECT_FALSE(decode(std::span<const std::uint8_t>{png.data(), 20}));
}

TEST(Image_loader_malformed, bad_ihdr_crc)
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    put_chunk(png, "IHDR", make_ihdr(2, 2), 0x5a5a5a5au);
    put_chunk(png, "IDAT", make_idat_2x2());
    put_chunk(png, "IEND", {});
    EXPECT_FALSE(decode(png));
}

TEST(Image_loader_malformed, bad_idat_crc)
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    put_chunk(png, "IHDR", make_ihdr(2, 2));
    put_chunk(png, "IDAT", make_idat_2x2(), 0x5a5a5a5au);
    put_chunk(png, "IEND", {});
    EXPECT_FALSE(decode(png));
}

TEST(Image_loader_malformed, absurd_dimensions)
{
    EXPECT_FALSE(decode(make_png(100000, 100000)));
    EXPECT_FALSE(decode(make_png(0x7fffffffu, 2)));
}

TEST(Image_loader_malformed, zero_dimensions)
{
    EXPECT_FALSE(decode(make_png(0, 2)));
    EXPECT_FALSE(decode(make_png(2, 0)));
}

TEST(Image_loader_malformed, dimensions_larger_than_data)
{
    EXPECT_FALSE(decode(make_png(64, 64)));
}

TEST(Image_loader_malformed, garbage_after_signature)
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    for (int i = 0; i < 64; ++i) {
        png.push_back(static_cast<std::uint8_t>((i * 37) + 11));
    }
    EXPECT_FALSE(decode(png));
}

TEST(Image_loader_malformed, signature_only)
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    EXPECT_FALSE(decode(png));
}

TEST(Image_loader_malformed, zero_length_idat)
{
    std::vector<std::uint8_t> png;
    put_signature(png);
    put_chunk(png, "IHDR", make_ihdr(2, 2));
    put_chunk(png, "IDAT", {});
    put_chunk(png, "IEND", {});
    EXPECT_FALSE(decode(png));
}

TEST(Image_loader_malformed, truncated_jpeg)
{
    const std::array<std::uint8_t, 24> jpeg{
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01,
        0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x11
    };
    EXPECT_FALSE(decode(jpeg));
}

TEST(Image_loader_malformed, garbage_jpeg)
{
    std::vector<std::uint8_t> jpeg{0xff, 0xd8, 0xff};
    for (int i = 0; i < 64; ++i) {
        jpeg.push_back(static_cast<std::uint8_t>((i * 53) + 7));
    }
    EXPECT_FALSE(decode(jpeg));
}

TEST(Image_loader_malformed, unknown_format)
{
    const std::array<std::uint8_t, 16> bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    EXPECT_FALSE(decode(bytes));
}

TEST(Image_loader_malformed, empty_input)
{
    EXPECT_FALSE(decode(std::span<const std::uint8_t>{}));
}
