#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr const char* c_array_vertex_source = R"glsl(
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_uv = (positions[gl_VertexID] * 0.5) + vec2(0.5);
}
)glsl";

// LAYER is injected as a define so each pass samples one fixed array layer.
constexpr const char* c_array_fragment_source = R"glsl(
layout(location = 0) in vec2 v_uv;
void main()
{
    out_color = texture(s_texture, vec3(v_uv, float(LAYER)));
}
)glsl";

} // namespace

// 2D array texture sampling. Create a texture_2d_array with 3 layers, fill each
// layer with a distinct solid color via copy_from_buffer's destination_slice
// (the buffer->image overload's baseArrayLayer). Then, in a separate render pass
// per layer, sample that layer through a sampler2DArray (layer selected by a
// GLSL define) and assert the rendered color matches the color uploaded to that
// layer. set_sampled_image builds a VK_IMAGE_VIEW_TYPE_2D_ARRAY view spanning
// all get_array_layer_count() layers, so the shader's vec3(uv, layer) lookup
// reaches each distinct slice. Mirrors test_texture_sample.cpp for the
// combined_image_sampler / set_sampled_image wiring.
TEST_F(Gpu_test, texture_2d_array_sample_layers)
{
    constexpr int layer_count = 3;
    constexpr int layer_size  = 4;  // each layer is 4x4 texels
    constexpr int out_width   = 16;
    constexpr int out_height  = 16;

    // Distinct solid color per layer (RGBA8).
    const std::array<std::array<uint8_t, 4>, layer_count> layer_colors{{
        {{ 200u,  30u,  40u, 255u }},  // layer 0
        {{  40u, 200u,  60u, 255u }},  // layer 1
        {{  50u,  70u, 210u, 255u }}   // layer 2
    }};

    erhe::graphics::Device& graphics_device = device();

    // Array texture: sampled (combined_image_sampler input) + transfer_dst
    // (copy_from_buffer target) + transfer_src (kept available for symmetry with
    // the readback helpers, though this test verifies via sampling).
    const std::shared_ptr<erhe::graphics::Texture> array_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = erhe::graphics::Texture_type::texture_2d_array,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
            .width             = layer_size,
            .height            = layer_size,
            .array_layer_count = layer_count,
            .debug_label       = erhe::utility::Debug_label{"array texture"}
        }
    );
    ASSERT_EQ(array_texture->get_array_layer_count(), layer_count);

    // Fill each layer from a host buffer via destination_slice. Each call
    // transitions its own layer subresource UNDEFINED -> TRANSFER_DST ->
    // SHADER_READ_ONLY and tracks the latter, so after all layers are filled the
    // texture is uniformly in shader_read_only_optimal, ready to sample.
    const std::size_t layer_bytes = static_cast<std::size_t>(layer_size) * static_cast<std::size_t>(layer_size) * 4u;
    for (int layer = 0; layer < layer_count; ++layer) {
        std::vector<uint8_t> layer_data(layer_bytes);
        for (std::size_t texel = 0; texel < (layer_bytes / 4u); ++texel) {
            layer_data[(texel * 4u) + 0] = layer_colors[layer][0];
            layer_data[(texel * 4u) + 1] = layer_colors[layer][1];
            layer_data[(texel * 4u) + 2] = layer_colors[layer][2];
            layer_data[(texel * 4u) + 3] = layer_colors[layer][3];
        }

        const std::shared_ptr<erhe::graphics::Buffer> layer_buffer =
            make_host_buffer(layer_bytes, erhe::graphics::Buffer_usage::transfer_src, "array layer source");
        {
            const std::span<std::byte> mapped = layer_buffer->map_bytes(0, layer_bytes);
            std::memcpy(mapped.data(), layer_data.data(), layer_bytes);
            layer_buffer->unmap();
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    layer_buffer.get(),
                    0,                                                  // source_offset
                    static_cast<std::uintptr_t>(layer_size) * 4u,       // source_bytes_per_row
                    static_cast<std::uintptr_t>(layer_bytes),           // source_bytes_per_image
                    glm::ivec3{layer_size, layer_size, 1},              // source_size
                    array_texture.get(),
                    static_cast<std::uintptr_t>(layer),                 // destination_slice (array layer)
                    0,                                                  // destination_level
                    glm::ivec3{0, 0, 0}                                 // destination_origin
                );
            }
        );
    }

    const erhe::graphics::Sampler sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = erhe::utility::Debug_label{"array sample nearest"}
        }
    };

    const erhe::graphics::Bind_group_layout sampler_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = "s_texture",
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d_array,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"array sample layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    // One pass per layer: sample that layer, render to a fresh output, read back.
    for (int layer = 0; layer < layer_count; ++layer) {
        const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(out_width, out_height);

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "texture_array_sample",
            .defines          = { { "LAYER", std::to_string(layer) } },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_array_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_array_fragment_source} }
            },
            .bind_group_layout = &sampler_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "array-sample shader (layer " << layer << ") failed to compile/link";
        erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = out_width;
        descriptor.render_target_height = out_height;
        descriptor.debug_label = erhe::utility::Debug_label{"array sample"};

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &sampler_layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
        ASSERT_TRUE(pipeline.is_valid()) << "array-sample pipeline (layer " << layer << ") is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, out_width, out_height);
                encoder.set_scissor_rect (0, 0, out_width, out_height);
                encoder.set_bind_group_layout(&sampler_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_sampled_image(0, *array_texture, sampler);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(out_width) * static_cast<std::size_t>(out_height) * 4u);

        int bad = 0;
        for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
            const bool ok =
                (std::abs(static_cast<int>(pixels[i + 0]) - static_cast<int>(layer_colors[layer][0])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 1]) - static_cast<int>(layer_colors[layer][1])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 2]) - static_cast<int>(layer_colors[layer][2])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 3]) - static_cast<int>(layer_colors[layer][3])) <= 2);
            if (!ok) {
                ++bad;
            }
        }
        EXPECT_EQ(bad, 0) << bad << " texels did not match layer " << layer << " color {"
                          << static_cast<int>(layer_colors[layer][0]) << ","
                          << static_cast<int>(layer_colors[layer][1]) << ","
                          << static_cast<int>(layer_colors[layer][2]) << ","
                          << static_cast<int>(layer_colors[layer][3]) << "}";
    }
}

namespace {

// texture_2d_array_sample_image: four 16x16 layers, each its own base color
// in a 4x4-texel checker (full / half intensity) with a white 2x2 marker in the
// texel-row-0 / column-0 corner, so the layer, its orientation and the texel
// addressing all show in the image.
constexpr int c_image_layer_count  = 4;
constexpr int c_image_layer_size   = 16;
constexpr int c_image_tile_size    = 64;
constexpr int c_image_output_size  = 2 * c_image_tile_size;

const std::array<std::array<int, 3>, c_image_layer_count> c_image_layer_colors{{
    {{ 230,  60,  50 }},
    {{  60, 220,  80 }},
    {{  70,  90, 240 }},
    {{ 230, 200,  60 }}
}};

[[nodiscard]] auto image_layer_texel(const int layer, const int x, const int y) -> std::array<uint8_t, 4>
{
    if ((x < 2) && (y < 2)) {
        return std::array<uint8_t, 4>{ 255u, 255u, 255u, 255u };
    }
    const bool                full  = ((((x / 4) + (y / 4)) % 2) == 0);
    const std::array<int, 3>& color = c_image_layer_colors[static_cast<std::size_t>(layer)];
    return std::array<uint8_t, 4>{
        static_cast<uint8_t>(full ? color[0] : (color[0] / 2)),
        static_cast<uint8_t>(full ? color[1] : (color[1] / 2)),
        static_cast<uint8_t>(full ? color[2] : (color[2] / 2)),
        255u
    };
}

// 2x2 tiles of 64x64 in image space: tile (tx, ty) shows layer tx + 2 * ty,
// its uv the position inside the tile.
constexpr const char* c_image_fragment_source = R"glsl(
void main()
{
    vec2  tile_position = IMAGE_POSITION / float(TILE_SIZE);
    ivec2 tile          = ivec2(floor(tile_position));
    float layer         = float(tile.x + (2 * tile.y));
    out_color = texture(s_texture, vec3(fract(tile_position), layer));
}
)glsl";

} // namespace

// agfx Sample2DArray, as a full image. A four-layer 16x16 texture_2d_array is
// seeded layer by layer through copy_from_buffer (instead of agfx's compute
// seed; pattern row 0 = texel row 0 = v 0) and sampled through a
// sampler2DArray with nearest filtering in a fullscreen fragment pass
// (doc/plans/graphics_tests_agfx_port.md 2.3) into a 128x128 image of 2x2
// tiles: tile (tx, ty) in image space (row 0 = image top) shows layer
// tx + 2 * ty magnified 4x. Output pixel (x, y) is exactly texel
// ((x % 64) / 4, (y % 64) / 4) of that layer; every pixel is checked, then the
// golden.
TEST_F(Gpu_test, texture_2d_array_sample_image)
{
    const std::shared_ptr<erhe::graphics::Texture> array_texture = make_sampled_texture(
        erhe::graphics::Texture_type::texture_2d_array, c_image_layer_size, c_image_layer_size, 1, c_image_layer_count, "array image source"
    );
    for (int layer = 0; layer < c_image_layer_count; ++layer) {
        std::vector<uint8_t> texels(static_cast<std::size_t>(c_image_layer_size) * static_cast<std::size_t>(c_image_layer_size) * 4u);
        for (int y = 0; y < c_image_layer_size; ++y) {
            for (int x = 0; x < c_image_layer_size; ++x) {
                const std::array<uint8_t, 4> texel = image_layer_texel(layer, x, y);
                std::memcpy(texels.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_image_layer_size)) + static_cast<std::size_t>(x)) * 4u), texel.data(), 4u);
            }
        }
        seed_subresource_rgba8(*array_texture, static_cast<unsigned int>(layer), 0, texels);
    }

    const erhe::graphics::Sampler sampler{
        device(),
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::nearest,
            .mag_filter   = erhe::graphics::Filter::nearest,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = {
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge
            },
            .debug_label  = erhe::utility::Debug_label{"array image nearest"}
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
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d_array,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"array image layout"},
            .uses_texture_heap = false
        }
    };
    const std::array<Sampled_image, 1> images{ Sampled_image{ .binding_point = 0, .texture = array_texture.get(), .sampler = &sampler } };
    const std::shared_ptr<erhe::graphics::Texture> output = render_fullscreen_pass(
        layout, c_image_fragment_source, { { "TILE_SIZE", std::to_string(c_image_tile_size) } }, images, c_image_output_size, c_image_output_size
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_image_output_size) * static_cast<std::size_t>(c_image_output_size) * 4u);

    constexpr int texel_pixels = c_image_tile_size / c_image_layer_size;
    std::vector<uint8_t> expected(pixels.size());
    for (int y = 0; y < c_image_output_size; ++y) {
        for (int x = 0; x < c_image_output_size; ++x) {
            const int layer = (x / c_image_tile_size) + (2 * (y / c_image_tile_size));
            const std::array<uint8_t, 4> texel = image_layer_texel(layer, (x % c_image_tile_size) / texel_pixels, (y % c_image_tile_size) / texel_pixels);
            std::memcpy(expected.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_image_output_size)) + static_cast<std::size_t>(x)) * 4u), texel.data(), 4u);
        }
    }
    const std::vector<uint8_t> image = memory_rows_to_image_rows(pixels, static_cast<std::size_t>(c_image_output_size) * 4u, c_image_output_size);
    expect_rgba8_near(image, expected, c_image_output_size, c_image_output_size, 0, "2d array image");
    expect_image_matches_golden("texture_2d_array_sample_image", c_image_output_size, c_image_output_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

} // namespace erhe::graphics::test
