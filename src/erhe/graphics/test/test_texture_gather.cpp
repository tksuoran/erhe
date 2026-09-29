#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Texture gather, ported from the agfx test TextureGather. agfx gathers from a
// compute shader into a storage image; here a fullscreen fragment pass writes
// the gathered values (doc/plans/graphics_tests_agfx_port.md 2.3).
//
// The source is an 8x8 format_8_vec4_unorm texture_2d whose red channel is
// distinct at every texel (seeded by copy_from_buffer, pattern row 0 = texel
// row 0 = v 0). The 144x72 output holds two 72x72 halves, each a 9x9 grid of
// 8x8-pixel cells in image space (row 0 = image top); cell (cx, cy) gathers
// the red component with
// textureGather(s_texture, vec2(cx, cy) / 8, 0), which puts uv exactly on the
// corner shared by texels (cx - 1, cy - 1), (cx, cy - 1), (cx - 1, cy) and
// (cx, cy). The GLSL gather order for the 2x2 footprint i0 = cx - 1,
// i1 = cx, j0 = cy - 1, j1 = cy is
//   x = (i0, j1), y = (i1, j1), z = (i1, j0), w = (i0, j0)
// The left half writes (x, y, z) as rgb, the right half (w, w, w), both with
// alpha 1 (the golden compare drops alpha). On the border cells (cx or cy = 0
// or 8) clamp_to_edge folds the outside footprint onto the edge texels. Every
// output texel is checked exactly against that CPU model, then the image
// against its golden.

namespace erhe::graphics::test {

namespace {

constexpr int c_source_size   = 8;
constexpr int c_cell_size     = 8;
constexpr int c_cell_count    = c_source_size + 1;
constexpr int c_half_size     = c_cell_count * c_cell_size;
constexpr int c_output_width  = 2 * c_half_size;
constexpr int c_output_height = c_half_size;

// Red is distinct per texel; green and blue are constant and never gathered.
[[nodiscard]] auto source_red(const int x, const int y) -> uint8_t
{
    return static_cast<uint8_t>(16 + (((y * c_source_size) + x) * 3));
}

constexpr const char* c_fragment_source = R"glsl(
void main()
{
    float half_size = float(CELL_SIZE * (SOURCE_SIZE + 1));
    vec2  position  = IMAGE_POSITION;
    bool  right     = (position.x >= half_size);
    if (right) {
        position.x = position.x - half_size;
    }
    vec2 cell     = floor(position / float(CELL_SIZE));
    vec4 gathered = textureGather(s_texture, cell / float(SOURCE_SIZE), 0);
    out_color = right ? vec4(gathered.www, 1.0) : vec4(gathered.xyz, 1.0);
}
)glsl";

} // namespace

// agfx TextureGather: textureGather of the red component at every four-texel
// corner of an 8x8 texture, borders included.
TEST_F(Gpu_test, texture_gather_red_corners)
{
    std::vector<uint8_t> pattern(static_cast<std::size_t>(c_source_size) * static_cast<std::size_t>(c_source_size) * 4u);
    for (int y = 0; y < c_source_size; ++y) {
        for (int x = 0; x < c_source_size; ++x) {
            const std::array<uint8_t, 4> texel{ source_red(x, y), 77u, 150u, 255u };
            std::memcpy(pattern.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_source_size)) + static_cast<std::size_t>(x)) * 4u), texel.data(), 4u);
        }
    }
    const std::shared_ptr<erhe::graphics::Texture> source = make_sampled_texture(
        erhe::graphics::Texture_type::texture_2d, c_source_size, c_source_size, 1, 0, "gather source"
    );
    seed_subresource_rgba8(*source, 0, 0, pattern);

    const erhe::graphics::Sampler sampler{
        device(),
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::linear,
            .mag_filter   = erhe::graphics::Filter::linear,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = {
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge
            },
            .debug_label  = erhe::utility::Debug_label{"gather sampler"}
        }
    };
    const erhe::graphics::Bind_group_layout layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = "s_texture",
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"gather layout"},
            .uses_texture_heap = false
        }
    };
    const std::array<Sampled_image, 1> images{ Sampled_image{ .binding_point = 0, .texture = source.get(), .sampler = &sampler } };
    const std::shared_ptr<erhe::graphics::Texture> output = render_fullscreen_pass(
        layout,
        c_fragment_source,
        { { "CELL_SIZE", std::to_string(c_cell_size) }, { "SOURCE_SIZE", std::to_string(c_source_size) } },
        images,
        c_output_width,
        c_output_height
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_output_width) * static_cast<std::size_t>(c_output_height) * 4u);

    const int last = c_source_size - 1;
    std::vector<uint8_t> expected(pixels.size());
    for (int y = 0; y < c_output_height; ++y) {
        for (int x = 0; x < c_output_width; ++x) {
            const bool right = (x >= c_half_size);
            const int  cx    = (right ? (x - c_half_size) : x) / c_cell_size;
            const int  cy    = y / c_cell_size;
            const int i0 = std::clamp(cx - 1, 0, last);
            const int i1 = std::clamp(cx,     0, last);
            const int j0 = std::clamp(cy - 1, 0, last);
            const int j1 = std::clamp(cy,     0, last);
            const uint8_t gx = source_red(i0, j1);
            const uint8_t gy = source_red(i1, j1);
            const uint8_t gz = source_red(i1, j0);
            const uint8_t gw = source_red(i0, j0);
            const std::array<uint8_t, 4> gathered = right
                ? std::array<uint8_t, 4>{ gw, gw, gw, 255u }
                : std::array<uint8_t, 4>{ gx, gy, gz, 255u };
            std::memcpy(expected.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_output_width)) + static_cast<std::size_t>(x)) * 4u), gathered.data(), 4u);
        }
    }
    const std::vector<uint8_t> image = memory_rows_to_image_rows(pixels, static_cast<std::size_t>(c_output_width) * 4u, c_output_height);
    expect_rgba8_near(image, expected, c_output_width, c_output_height, 0, "gather red");
    expect_image_matches_golden("texture_gather_red_corners", c_output_width, c_output_height, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

} // namespace erhe::graphics::test
