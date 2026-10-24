#include "gpu_test_fixture.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
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
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A dedicated sampler read in the vertex stage, behind a buffer binding and a
// fragment-stage dedicated sampler, so its texture slot (GLSL binding 2) and
// its sampler slot (1) are both non-zero: the backends must place the vertex
// function's [[texture(N)]] / [[sampler(M)]] (Metal) or descriptor (Vulkan)
// where set_sampled_image binds it (Bind_group_layout_binding::stage_flags).
//
// Layout: UBO "Params" at binding 0 (read in the vertex stage), s_decoy at
// sampler binding_point 0 (fragment), s_source at sampler binding_point 1
// (vertex). The vertex shader draws one point per pixel of a 4x4 target and
// samples s_source with textureLod at the centre of texel column MIRROR - x
// (MIRROR from the UBO) and one texture height past row y: s_source's
// sampler clamps to edge, so every pixel reads row GRID - 1, while s_decoy's
// sampler repeats and would read row y. The fragment shader multiplies by
// s_decoy's white texel (0, 0). A vertex stage that reads the decoy texture,
// the decoy's sampler or an unbound slot, or a fragment stage that reads the
// vertex texture, changes the image. Every pixel is checked exactly.

namespace erhe::graphics::test {

namespace {

constexpr int c_grid = 4;

constexpr const char* c_vertex_source = R"glsl(
out gl_PerVertex {
    vec4  gl_Position;
    float gl_PointSize;
};
layout(location = 0) flat out vec4 v_color;
float pixel_center_ndc(float pixel)
{
    return ((pixel + 0.5) / float(GRID)) * 2.0 - 1.0;
}
void main()
{
    int gx       = gl_VertexID % GRID;
    int gy       = gl_VertexID / GRID;
    vec2 uv      = vec2((float(int(Params.mirror) - gx) + 0.5) / float(GRID), ((float(gy) + 0.5) / float(GRID)) + 1.0);
    v_color      = textureLod(s_source, uv, 0.0);
    gl_Position  = vec4(pixel_center_ndc(float(gx)), float(Y_SIGN) * pixel_center_ndc(float(gy)), 0.0, 1.0);
    gl_PointSize = 1.0;
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) flat in vec4 v_color;
void main()
{
    out_color = v_color * texelFetch(s_decoy, ivec2(0, 0), 0);
}
)glsl";

[[nodiscard]] auto source_texel(const int x, const int y) -> std::array<uint8_t, 4>
{
    return std::array<uint8_t, 4>{
        static_cast<uint8_t>(20 + (x * 60)),
        static_cast<uint8_t>(10 + (y * 70)),
        static_cast<uint8_t>(200 - (x * 10) - (y * 30)),
        255u
    };
}

[[nodiscard]] auto decoy_texel(const int x, const int y) -> std::array<uint8_t, 4>
{
    if ((x == 0) && (y == 0)) {
        return std::array<uint8_t, 4>{ 255u, 255u, 255u, 255u };
    }
    return std::array<uint8_t, 4>{ 7u, 7u, 7u, 7u };
}

template <typename Texel_function>
[[nodiscard]] auto make_texels(Texel_function texel_function) -> std::vector<uint8_t>
{
    std::vector<uint8_t> texels(static_cast<std::size_t>(c_grid) * static_cast<std::size_t>(c_grid) * 4u);
    for (int y = 0; y < c_grid; ++y) {
        for (int x = 0; x < c_grid; ++x) {
            const std::array<uint8_t, 4> texel = texel_function(x, y);
            std::memcpy(texels.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_grid)) + static_cast<std::size_t>(x)) * 4u), texel.data(), 4u);
        }
    }
    return texels;
}

} // namespace

TEST_F(Gpu_test, vertex_stage_sampler_behind_buffer_and_fragment_sampler)
{
    erhe::graphics::Device& graphics_device = device();
    constexpr uint32_t    mirror    = c_grid - 1;
    constexpr std::size_t ubo_bytes = 16; // std140 rounds a single-uint block up to 16

    erhe::graphics::Shader_resource params_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Params",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::uniform_block
        }
    };
    params_block.add_uint("mirror");

    const erhe::graphics::Bind_group_layout layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = erhe::graphics::Binding_type::uniform_buffer,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::vertex
                },
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point  = 0u,
                    .type           = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect = erhe::graphics::Sampler_aspect::color,
                    .name           = "s_decoy",
                    .glsl_type      = erhe::graphics::Glsl_type::sampler_2d,
                    .stage_flags    = erhe::graphics::Shader_stage_flags::fragment
                },
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point  = 1u,
                    .type           = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect = erhe::graphics::Sampler_aspect::color,
                    .name           = "s_source",
                    .glsl_type      = erhe::graphics::Glsl_type::sampler_2d,
                    .stage_flags    = erhe::graphics::Shader_stage_flags::vertex
                }
            },
            .debug_label       = erhe::utility::Debug_label{"vertex stage sampler layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Sampler source_sampler{
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
            .debug_label  = erhe::utility::Debug_label{"vertex stage sampler clamp"}
        }
    };
    const erhe::graphics::Sampler decoy_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::nearest,
            .mag_filter   = erhe::graphics::Filter::nearest,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = {
                erhe::graphics::Sampler_address_mode::repeat,
                erhe::graphics::Sampler_address_mode::repeat,
                erhe::graphics::Sampler_address_mode::repeat
            },
            .debug_label  = erhe::utility::Debug_label{"vertex stage sampler repeat"}
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };
    const erhe::math::Coordinate_conventions& conventions = graphics_device.get_info().coordinate_conventions;
    const char* const y_sign = (conventions.texture_origin == erhe::math::Texture_origin::top_left) ? "-1.0" : "1.0";
    erhe::graphics::Shader_stages_create_info create_info{
        .name             = "vertex_stage_sampler",
        .defines          = {
            { "GRID",   std::to_string(c_grid) },
            { "Y_SIGN", y_sign                 }
        },
        .interface_blocks = { &params_block },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, create_info);
    ASSERT_TRUE(prototype.is_valid()) << "vertex_stage_sampler: shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

    const std::shared_ptr<erhe::graphics::Texture> source = make_sampled_texture(erhe::graphics::Texture_type::texture_2d, c_grid, c_grid, 1, 0, "vertex stage sampler source");
    const std::shared_ptr<erhe::graphics::Texture> decoy  = make_sampled_texture(erhe::graphics::Texture_type::texture_2d, c_grid, c_grid, 1, 0, "vertex stage sampler decoy");
    seed_subresource_rgba8(*source, 0, 0, make_texels(source_texel));
    seed_subresource_rgba8(*decoy,  0, 0, make_texels(decoy_texel));

    const std::shared_ptr<erhe::graphics::Buffer> ubo = make_host_buffer(ubo_bytes, erhe::graphics::Buffer_usage::uniform, "vertex stage sampler params");
    {
        const std::span<std::byte> mapped = ubo->map_bytes(0, ubo_bytes);
        std::memset(mapped.data(), 0, ubo_bytes);
        std::memcpy(mapped.data(), &mirror, sizeof(mirror));
        ubo->unmap();
    }

    const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(c_grid, c_grid);
    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = output.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 0.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = c_grid;
    descriptor.render_target_height = c_grid;
    descriptor.debug_label = erhe::utility::Debug_label{"vertex stage sampler pass"};

    erhe::graphics::Render_pipeline_create_info pipeline_info;
    pipeline_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::point;
    pipeline_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_info.base.bind_group_layout                 = &layout;
    pipeline_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_info.shader_stages                          = &shader_stages;
    pipeline_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_info};
    ASSERT_TRUE(pipeline.is_valid()) << "vertex_stage_sampler: pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass              render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder   encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_grid, c_grid);
            encoder.set_scissor_rect (0, 0, c_grid, c_grid);
            encoder.set_bind_group_layout(&layout);
            encoder.set_render_pipeline(pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::uniform, ubo.get(), 0, ubo_bytes, 0);
            encoder.set_sampled_image(0, *decoy,  decoy_sampler);
            encoder.set_sampled_image(1, *source, source_sampler);
            encoder.draw_primitives(erhe::graphics::Primitive_type::point, 0, c_grid * c_grid);
        }
    );

    // Pixel (gx, gy) of the memory rows holds source texel (MIRROR - gx,
    // GRID - 1): row clamped to the edge.
    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_grid * c_grid) * 4u);
    int mismatches = 0;
    int first      = -1;
    for (int gy = 0; gy < c_grid; ++gy) {
        for (int gx = 0; gx < c_grid; ++gx) {
            const std::array<uint8_t, 4> expected = source_texel(static_cast<int>(mirror) - gx, c_grid - 1);
            const std::size_t            base     = ((static_cast<std::size_t>(gy) * static_cast<std::size_t>(c_grid)) + static_cast<std::size_t>(gx)) * 4u;
            if (std::memcmp(pixels.data() + base, expected.data(), 4u) != 0) {
                if (mismatches == 0) {
                    first = gx + (gy * c_grid);
                }
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << (c_grid * c_grid) << " pixels differ; first at pixel " << first;
}

} // namespace erhe::graphics::test
