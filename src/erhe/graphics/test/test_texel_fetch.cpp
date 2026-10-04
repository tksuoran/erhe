#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

// texelFetch, ported from the agfx tests ComputeTextureLoad2D,
// ComputeTextureLoad2DArray and ComputeTextureLoad3D. agfx loads texels in a
// compute shader and writes a storage image; here a fullscreen fragment pass
// does the texelFetch and writes the color target
// (doc/plans/graphics_tests_agfx_port.md 2.3).
//
// Sources are 16x16 format_8_vec4_unorm images (one per layer / z-slice, four
// of them for the array and 3D cases) seeded by copy_from_buffer, pattern row
// 0 = texel row 0. Each is a red ramp over x, a green ramp over y and a per
// layer blue, with a white 2x2 marker at texel (0, 0). Every output texel
// fetches through three transforms that each show up in the image if the
// addressing is wrong:
//   - mirror:   texel x = 15 - x (the marker moves to the right edge)
//   - swizzle:  the output is fetched.gbra (red <- green, green <- blue,
//               blue <- red)
//   - reversal: the array layer / z-slice is 3 - k for tile k
// Output pixels are 4x magnified texels in image space (row 0 = image top):
// 64x64 for the 2D case, 128x128 of 2x2 tiles (tile k = tx + 2 * ty) for the
// array and 3D cases. Every output texel is checked exactly against the CPU
// model, then the image against its golden.

namespace erhe::graphics::test {

namespace {

constexpr int c_source_size  = 16;
constexpr int c_layer_count  = 4;
constexpr int c_texel_pixels = 4;
constexpr int c_tile_size    = c_source_size * c_texel_pixels;

constexpr std::array<int, c_layer_count> c_layer_blue{ 40, 110, 180, 250 };

[[nodiscard]] auto source_texel(const int layer, const int x, const int y) -> std::array<uint8_t, 4>
{
    if ((x < 2) && (y < 2)) {
        return std::array<uint8_t, 4>{ 255u, 255u, 255u, 255u };
    }
    return std::array<uint8_t, 4>{
        static_cast<uint8_t>((x * 15) + 10),
        static_cast<uint8_t>((y * 15) + 10),
        static_cast<uint8_t>(c_layer_blue[static_cast<std::size_t>(layer)]),
        255u
    };
}

[[nodiscard]] auto make_layer(const int layer) -> std::vector<uint8_t>
{
    std::vector<uint8_t> texels(static_cast<std::size_t>(c_source_size) * static_cast<std::size_t>(c_source_size) * 4u);
    for (int y = 0; y < c_source_size; ++y) {
        for (int x = 0; x < c_source_size; ++x) {
            const std::array<uint8_t, 4> texel = source_texel(layer, x, y);
            std::memcpy(texels.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_source_size)) + static_cast<std::size_t>(x)) * 4u), texel.data(), 4u);
        }
    }
    return texels;
}

enum class Fetch_kind : unsigned int { texture_2d, texture_2d_array, texture_3d };

// FETCH selects the texelFetch coordinate: 0 = 2D, 1 = 2D array, 2 = 3D.
//
// The clamp on layer is a no-op (every tile maps to a valid layer) that works
// around an NVIDIA GLSL compiler defect: texelFetch on a sampler3D returns
// zeros when the z coordinate is written as (LAYER_COUNT - 1) - (tile.x + ...)
// from the integer-divided pixel coordinate, and reads correctly when the same
// expression is clamped (doc/reference/nvidia_texel_fetch_3d_driver_report.md,
// with the standalone reproduction and the variant table).
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    ivec2 pixel = ivec2(floor(IMAGE_POSITION));
    ivec2 tile  = pixel / (SOURCE_SIZE * TEXEL_PIXELS);
    ivec2 local = (pixel % (SOURCE_SIZE * TEXEL_PIXELS)) / TEXEL_PIXELS;
    ivec2 texel = ivec2((SOURCE_SIZE - 1) - local.x, local.y);
    int   layer = clamp((LAYER_COUNT - 1) - (tile.x + (2 * tile.y)), 0, LAYER_COUNT - 1);
#if FETCH == 0
    vec4 fetched = texelFetch(s_texture, texel, 0);
#else
    vec4 fetched = texelFetch(s_texture, ivec3(texel, layer), 0);
#endif
    out_color = fetched.gbra;
}
)glsl";

} // namespace

class Texel_fetch_test : public Gpu_test
{
protected:
    void check_fetch(const Fetch_kind kind, const char* golden_name)
    {
        erhe::graphics::Device& graphics_device = device();

        const bool single      = (kind == Fetch_kind::texture_2d);
        const int  output_size = single ? c_tile_size : (2 * c_tile_size);

        std::shared_ptr<erhe::graphics::Texture> source;
        erhe::graphics::Glsl_type                glsl_type = erhe::graphics::Glsl_type::sampler_2d;
        switch (kind) {
            case Fetch_kind::texture_2d: {
                source = make_sampled_texture(erhe::graphics::Texture_type::texture_2d, c_source_size, c_source_size, 1, 0, "texel fetch 2d");
                seed_subresource_rgba8(*source, 0, 0, make_layer(0));
                glsl_type = erhe::graphics::Glsl_type::sampler_2d;
                break;
            }
            case Fetch_kind::texture_2d_array: {
                source = make_sampled_texture(erhe::graphics::Texture_type::texture_2d_array, c_source_size, c_source_size, 1, c_layer_count, "texel fetch 2d array");
                for (int layer = 0; layer < c_layer_count; ++layer) {
                    seed_subresource_rgba8(*source, static_cast<unsigned int>(layer), 0, make_layer(layer));
                }
                glsl_type = erhe::graphics::Glsl_type::sampler_2d_array;
                break;
            }
            case Fetch_kind::texture_3d:
            default: {
                source = make_sampled_texture(erhe::graphics::Texture_type::texture_3d, c_source_size, c_source_size, c_layer_count, 0, "texel fetch 3d");
                const std::size_t slice_bytes  = static_cast<std::size_t>(c_source_size) * static_cast<std::size_t>(c_source_size) * 4u;
                const std::size_t volume_bytes = slice_bytes * static_cast<std::size_t>(c_layer_count);
                const std::shared_ptr<erhe::graphics::Buffer> volume_buffer =
                    make_host_buffer(volume_bytes, erhe::graphics::Buffer_usage::transfer_src, "texel fetch 3d data");
                {
                    const std::span<std::byte> mapped = volume_buffer->map_bytes(0, volume_bytes);
                    for (int z = 0; z < c_layer_count; ++z) {
                        const std::vector<uint8_t> slice = make_layer(z);
                        std::memcpy(mapped.data() + (static_cast<std::size_t>(z) * slice_bytes), slice.data(), slice_bytes);
                    }
                    volume_buffer->unmap();
                }
                submit_and_wait(
                    [&](erhe::graphics::Command_buffer& command_buffer) {
                        erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
                        blit.copy_from_buffer(
                            erhe::graphics::Buffer_texel_location{.buffer = volume_buffer.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(c_source_size) * 4u, .bytes_per_image = static_cast<std::uintptr_t>(slice_bytes)},
                            glm::ivec3{c_source_size, c_source_size, c_layer_count},
                            erhe::graphics::Texture_location{.texture = source.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}}
                        );
                    }
                );
                glsl_type = erhe::graphics::Glsl_type::sampler_3d;
                break;
            }
        }

        const erhe::graphics::Sampler sampler{
            graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter   = erhe::graphics::Filter::nearest,
                .mag_filter   = erhe::graphics::Filter::nearest,
                .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .address_mode = {
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge
                },
                .debug_label  = erhe::utility::Debug_label{"texel fetch sampler"}
            }
        };
        const erhe::graphics::Bind_group_layout layout{
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 0,
                        .type          = erhe::graphics::Binding_type::combined_image_sampler,
                        .name          = "s_texture",
                        .glsl_type     = glsl_type,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"texel fetch layout"},
                .uses_texture_heap = false
            }
        };
        const int fetch = static_cast<int>(kind);
        const std::array<Sampled_image, 1> images{ Sampled_image{ .binding_point = 0, .texture = source.get(), .sampler = &sampler } };
        const std::shared_ptr<erhe::graphics::Texture> output = render_fullscreen_pass(
            layout,
            c_fragment_source,
            {
                { "FETCH",        std::to_string(fetch)          },
                { "SOURCE_SIZE",  std::to_string(c_source_size)  },
                { "TEXEL_PIXELS", std::to_string(c_texel_pixels) },
                { "LAYER_COUNT",  std::to_string(c_layer_count)  }
            },
            images,
            output_size,
            output_size
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(output_size) * static_cast<std::size_t>(output_size) * 4u);

        std::vector<uint8_t> expected(pixels.size());
        for (int y = 0; y < output_size; ++y) {
            for (int x = 0; x < output_size; ++x) {
                const int tile  = (x / c_tile_size) + (2 * (y / c_tile_size));
                const int layer = single ? 0 : ((c_layer_count - 1) - tile);
                const int tx    = (c_source_size - 1) - ((x % c_tile_size) / c_texel_pixels);
                const int ty    = (y % c_tile_size) / c_texel_pixels;
                const std::array<uint8_t, 4> fetched = source_texel(layer, tx, ty);
                const std::array<uint8_t, 4> swizzled{ fetched[1], fetched[2], fetched[0], fetched[3] };
                std::memcpy(expected.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(output_size)) + static_cast<std::size_t>(x)) * 4u), swizzled.data(), 4u);
            }
        }
        const std::vector<uint8_t> image = memory_rows_to_image_rows(pixels, static_cast<std::size_t>(output_size) * 4u, output_size);
        expect_rgba8_near(image, expected, output_size, output_size, 0, golden_name);
        expect_image_matches_golden(golden_name, output_size, output_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
    }
};

// agfx ComputeTextureLoad2D: mirrored, swizzled texelFetch from a texture_2d.
TEST_F(Texel_fetch_test, texture_2d)
{
    check_fetch(Fetch_kind::texture_2d, "texel_fetch_2d");
}

// agfx ComputeTextureLoad2DArray: mirrored, swizzled texelFetch from a
// four-layer texture_2d_array, layers in reverse tile order.
TEST_F(Texel_fetch_test, texture_2d_array)
{
    check_fetch(Fetch_kind::texture_2d_array, "texel_fetch_2d_array");
}

// agfx ComputeTextureLoad3D: mirrored, swizzled texelFetch from a 16x16x4
// texture_3d, z-slices in reverse tile order.
TEST_F(Texel_fetch_test, texture_3d)
{
    check_fetch(Fetch_kind::texture_3d, "texel_fetch_3d");
}

} // namespace erhe::graphics::test
