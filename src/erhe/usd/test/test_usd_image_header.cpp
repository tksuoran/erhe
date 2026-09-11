// The image header probe behind the UsdPreviewSurface `sourceColorSpace =
// auto` rule (src/erhe/usd/notes.md, Materials): sRGB when the image is
// 8-bit with 3 or 4 components, data otherwise, read off the file header
// with no decode.

#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/image_header.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto probe_file(const char* file_name) -> erhe::usd::Image_header
{
    return erhe::usd::probe_image_header(test_data_path(file_name));
}

// PNG signature plus an IHDR chunk with the given bit depth and color type.
[[nodiscard]] auto png_header(const std::uint8_t bit_depth, const std::uint8_t color_type) -> std::vector<std::uint8_t>
{
    return {
        0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au,
        0x00u, 0x00u, 0x00u, 0x0du, 'I', 'H', 'D', 'R',
        0x00u, 0x00u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x02u,
        bit_depth, color_type, 0x00u, 0x00u, 0x00u
    };
}

} // anonymous namespace

TEST(Image_header_probe, a_png_header_names_depth_and_components)
{
    const erhe::usd::Image_header rgba = erhe::usd::probe_image_header(png_header(8, 6));
    EXPECT_TRUE(rgba.known);
    EXPECT_EQ(rgba.bits_per_component, 8u);
    EXPECT_EQ(rgba.component_count, 4u);

    const erhe::usd::Image_header rgb16 = erhe::usd::probe_image_header(png_header(16, 2));
    EXPECT_TRUE(rgb16.known);
    EXPECT_EQ(rgb16.bits_per_component, 16u);
    EXPECT_EQ(rgb16.component_count, 3u);

    // A palette is 8-bit RGB entries whatever the index depth.
    const erhe::usd::Image_header palette = erhe::usd::probe_image_header(png_header(4, 3));
    EXPECT_TRUE(palette.known);
    EXPECT_EQ(palette.bits_per_component, 8u);
    EXPECT_EQ(palette.component_count, 3u);
}

TEST(Image_header_probe, a_jpeg_frame_header_is_found_past_metadata_segments)
{
    // SOI, an APP1 segment of 6 bytes, then SOF0: precision 8, 2x2, 3 components.
    const std::vector<std::uint8_t> bytes{
        0xffu, 0xd8u,
        0xffu, 0xe1u, 0x00u, 0x06u, 'E', 'x', 'i', 'f',
        0xffu, 0xc0u, 0x00u, 0x0bu, 0x08u, 0x00u, 0x02u, 0x00u, 0x02u, 0x03u, 0x01u, 0x11u, 0x00u
    };
    const erhe::usd::Image_header header = erhe::usd::probe_image_header(bytes);
    EXPECT_TRUE(header.known);
    EXPECT_EQ(header.bits_per_component, 8u);
    EXPECT_EQ(header.component_count, 3u);
}

TEST(Image_header_probe, unreadable_bytes_are_not_known)
{
    const std::vector<std::uint8_t> truncated_png{0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au, 0x00u};
    EXPECT_FALSE(erhe::usd::probe_image_header(truncated_png).known);

    // A JPEG that reaches its scan without a frame header.
    const std::vector<std::uint8_t> scan_first{0xffu, 0xd8u, 0xffu, 0xdau, 0x00u, 0x02u};
    EXPECT_FALSE(erhe::usd::probe_image_header(scan_first).known);

    const std::vector<std::uint8_t> text{'#', 'u', 's', 'd', 'a', ' ', '1', '.', '0', '\n', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_FALSE(erhe::usd::probe_image_header(text).known);
    EXPECT_FALSE(erhe::usd::probe_image_header(std::vector<std::uint8_t>{}).known);
    EXPECT_FALSE(erhe::usd::probe_image_header(test_data_path("does_not_exist.png")).known);
}

TEST(Image_header_probe, the_auto_rule_reads_each_container)
{
    EXPECT_TRUE (erhe::usd::is_srgb_by_auto_rule(probe_file("images/auto_rgba.png")));
    EXPECT_TRUE (erhe::usd::is_srgb_by_auto_rule(probe_file("images/auto_rgb.jpg")));
    EXPECT_TRUE (erhe::usd::is_srgb_by_auto_rule(probe_file("images/auto_palette.png")));
    EXPECT_TRUE (erhe::usd::is_srgb_by_auto_rule(probe_file("images/auto_rgb.bmp")));
    EXPECT_TRUE (erhe::usd::is_srgb_by_auto_rule(probe_file("images/auto_rgba.tga")));
    EXPECT_FALSE(erhe::usd::is_srgb_by_auto_rule(probe_file("images/auto_gray.png")));
    EXPECT_FALSE(erhe::usd::is_srgb_by_auto_rule(probe_file("images/auto_gray16.png")));
    EXPECT_FALSE(erhe::usd::is_srgb_by_auto_rule(erhe::usd::Image_header{}));
}

// A UsdUVTexture without `inputs:sourceColorSpace` is `auto`: the image's
// own header decides. An authored color space decides on its own.
TEST(Image_header_probe, an_auto_color_space_is_resolved_from_the_image)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments arguments{
        .path          = test_data_path("color_space_auto.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(arguments);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const auto srgb_of = [&result](const std::string& name, const bool expected) {
        std::size_t seen = 0;
        for (const erhe::usd::Usd_image& image : result.data.images) {
            if (image.name != name) {
                continue;
            }
            ++seen;
            EXPECT_EQ(image.srgb, expected) << name;
            EXPECT_TRUE(std::filesystem::exists(image.path)) << image.path.generic_string();
        }
        EXPECT_GE(seen, 1u) << name << " is not among the images";
    };
    srgb_of("auto_rgb.jpg",      true);
    srgb_of("auto_palette.png",  true);
    srgb_of("auto_rgb.bmp",      true);
    srgb_of("auto_rgba.tga",     true);
    srgb_of("auto_gray.png",     false);
    srgb_of("auto_gray16.png",   false);

    // auto_rgba.png is read twice: once as `auto` (8-bit RGBA, so sRGB) and
    // once authored `raw`.
    std::size_t srgb_count = 0;
    std::size_t raw_count  = 0;
    for (const erhe::usd::Usd_image& image : result.data.images) {
        if (image.name != "auto_rgba.png") {
            continue;
        }
        if (image.srgb) {
            ++srgb_count;
        } else {
            ++raw_count;
        }
    }
    EXPECT_EQ(srgb_count, 1u);
    EXPECT_EQ(raw_count,  1u);
}
