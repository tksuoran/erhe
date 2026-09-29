#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
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

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Sample the bound texture at a fixed UV provided through the SAMPLE_UV define
// (a vec2 literal). The whole output therefore carries one texture() result.
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = texture(s_texture, SAMPLE_UV);
}
)glsl";

constexpr const char* c_sampler_binding_name = "s_texture";

// Build the combined-image-sampler bind group layout shared by every sampling
// pass in this file.
auto make_sampler_layout(erhe::graphics::Device& device, const char* debug_label) -> erhe::graphics::Bind_group_layout
{
    return erhe::graphics::Bind_group_layout{
        device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = c_sampler_binding_name,
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{debug_label},
            .uses_texture_heap = false
        }
    };
}

// Build a sampling shader whose fragment samples at the given UV literal.
auto make_sampling_shader(
    erhe::graphics::Device&                  device,
    const erhe::graphics::Bind_group_layout& layout,
    const erhe::graphics::Fragment_outputs&  fragment_outputs,
    const char*                              sample_uv_glsl,
    const char*                              name
) -> erhe::graphics::Shader_stages
{
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = name,
        .defines          = { { "SAMPLE_UV", sample_uv_glsl } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device, shader_create_info);
    EXPECT_TRUE(prototype.is_valid()) << name << " shader failed to compile/link";
    return erhe::graphics::Shader_stages{device, std::move(prototype)};
}

} // namespace

// Sampler linear filter: upload a 2x1 texture with two distinct texel values
// (black texel0, white texel1), sample at u = 0.5 -- exactly halfway between the
// two texel centres (0.25 and 0.75) -- with Filter::linear, and assert the
// result is their interpolated midpoint (~128 gray), within a tolerance.
// Contrasts with texture_sample_nearest, where the nearest filter would snap to
// one texel's value with no interpolation.
TEST_F(Gpu_test, sampler_linear_filter_midpoint)
{
    constexpr int src_width  = 2;
    constexpr int src_height = 1;
    constexpr int out_size   = 1;

    erhe::graphics::Device& graphics_device = device();

    // texel0 = black {0,0,0,255}, texel1 = white {255,255,255,255}.
    const std::array<uint8_t, 8> source_texels{
        0u,   0u,   0u,   255u,
        255u, 255u, 255u, 255u
    };

    // Sampled (heap) + transfer_dst for the upload.
    const std::shared_ptr<erhe::graphics::Texture> source =
        make_color_target(src_width, src_height, erhe::dataformat::Format::format_8_vec4_unorm, /*include_transfer_dst=*/true);
    const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(out_size, out_size);

    // Upload the pattern, then move the source into shader_read_only_optimal.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.upload_to_texture(
                *source,
                0, 0, 0,
                src_width,
                src_height,
                erhe::dataformat::Format::format_8_vec4_unorm,
                source_texels.data(),
                src_width * 4
            );
            command_buffer.transition_texture_layout(*source, erhe::graphics::Image_layout::shader_read_only_optimal);
        }
    );

    const erhe::graphics::Sampler sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::linear,
            .mag_filter   = erhe::graphics::Filter::linear,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = {
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge
            },
            .debug_label  = erhe::utility::Debug_label{"sampler linear"}
        }
    };

    const erhe::graphics::Bind_group_layout sampler_layout = make_sampler_layout(graphics_device, "sampler linear layout");
    const erhe::graphics::Fragment_outputs  fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    // Sample exactly between the two texel centres.
    erhe::graphics::Shader_stages shader_stages = make_sampling_shader(graphics_device, sampler_layout, fragment_outputs, "vec2(0.5, 0.5)", "sampler_linear");

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = output.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = out_size;
    descriptor.render_target_height = out_size;
    descriptor.debug_label = erhe::utility::Debug_label{"sampler linear sample"};

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
    ASSERT_TRUE(pipeline.is_valid()) << "sampler linear pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, out_size, out_size);
            encoder.set_scissor_rect (0, 0, out_size, out_size);
            encoder.set_bind_group_layout(&sampler_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.set_sampled_image(0, *source, sampler);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(out_size) * static_cast<std::size_t>(out_size) * 4u);

    // Midpoint of black and white is ~128. Allow a small tolerance for rounding /
    // sRGB-vs-unorm interpolation behaviour.
    const int r = pixels[0];
    const int g = pixels[1];
    const int b = pixels[2];
    const int a = pixels[3];
    EXPECT_GE(r, 120); EXPECT_LE(r, 136) << "linear-filtered R is not the black/white midpoint";
    EXPECT_GE(g, 120); EXPECT_LE(g, 136) << "linear-filtered G is not the black/white midpoint";
    EXPECT_GE(b, 120); EXPECT_LE(b, 136) << "linear-filtered B is not the black/white midpoint";
    EXPECT_GE(a, 253) << "alpha (255 in both texels) should stay opaque";
}

// Sampler address modes: with a 2x1 texture (texel0 = red, texel1 = green) and a
// nearest filter, sample at out-of-[0,1] coordinates with clamp_to_edge, repeat,
// and mirrored_repeat, and assert the fetched texel matches the wrapped
// coordinate for each mode.
//
// Two sample points uniquely identify each mode:
//   u = 1.25                          u = 1.75
//   clamp -> 1.0     -> texel1 (G)    clamp -> 1.0       -> texel1 (G)
//   repeat-> 0.25    -> texel0 (R)    repeat-> 0.75      -> texel1 (G)
//   mirror-> 2-1.25=0.75 -> texel1(G) mirror-> 2-1.75=0.25 -> texel0 (R)
// so the (1.25, 1.75) signatures are clamp=(G,G), repeat=(R,G), mirror=(G,R) --
// all distinct.
TEST_F(Gpu_test, sampler_address_modes)
{
    constexpr int src_width  = 2;
    constexpr int src_height = 1;
    constexpr int out_size   = 1;

    erhe::graphics::Device& graphics_device = device();

    // texel0 = red {255,0,0,255}, texel1 = green {0,255,0,255}.
    const std::array<uint8_t, 8> source_texels{
        255u, 0u,   0u, 255u,
        0u,   255u, 0u, 255u
    };

    const std::shared_ptr<erhe::graphics::Texture> source =
        make_color_target(src_width, src_height, erhe::dataformat::Format::format_8_vec4_unorm, /*include_transfer_dst=*/true);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.upload_to_texture(
                *source,
                0, 0, 0,
                src_width,
                src_height,
                erhe::dataformat::Format::format_8_vec4_unorm,
                source_texels.data(),
                src_width * 4
            );
            command_buffer.transition_texture_layout(*source, erhe::graphics::Image_layout::shader_read_only_optimal);
        }
    );

    const erhe::graphics::Bind_group_layout sampler_layout = make_sampler_layout(graphics_device, "sampler address layout");
    const erhe::graphics::Fragment_outputs  fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    enum class Texel { red, green };

    // Run one sampling pass at the given out-of-range u with the given address
    // mode, and return whether the single output texel reads red or green.
    auto sample_with = [&](
        const erhe::graphics::Sampler_address_mode address_mode,
        const char*                                sample_uv_glsl,
        const char*                                name
    ) -> Texel {
        const erhe::graphics::Sampler sampler{
            graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter   = erhe::graphics::Filter::nearest,
                .mag_filter   = erhe::graphics::Filter::nearest,
                .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .address_mode = { address_mode, address_mode, address_mode },
                .debug_label  = erhe::utility::Debug_label{name}
            }
        };

        const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(out_size, out_size);

        erhe::graphics::Shader_stages shader_stages = make_sampling_shader(graphics_device, sampler_layout, fragment_outputs, sample_uv_glsl, name);

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = out_size;
        descriptor.render_target_height = out_size;
        descriptor.debug_label = erhe::utility::Debug_label{name};

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
        EXPECT_TRUE(pipeline.is_valid()) << name << " pipeline is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, out_size, out_size);
                encoder.set_scissor_rect (0, 0, out_size, out_size);
                encoder.set_bind_group_layout(&sampler_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_sampled_image(0, *source, sampler);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        EXPECT_EQ(pixels.size(), static_cast<std::size_t>(out_size) * static_cast<std::size_t>(out_size) * 4u);
        const int r = pixels[0];
        const int g = pixels[1];
        // Red texel is {255,0,0}; green texel is {0,255,0}: classify by dominant channel.
        return (r > g) ? Texel::red : Texel::green;
    };

    // clamp_to_edge: both 1.25 and 1.75 clamp to the last texel (green).
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::clamp_to_edge,   "vec2(1.25, 0.5)", "clamp_1_25"),  Texel::green) << "clamp(1.25) should read the last texel (green)";
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::clamp_to_edge,   "vec2(1.75, 0.5)", "clamp_1_75"),  Texel::green) << "clamp(1.75) should read the last texel (green)";

    // repeat: 1.25 -> 0.25 (red), 1.75 -> 0.75 (green).
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::repeat,          "vec2(1.25, 0.5)", "repeat_1_25"), Texel::red)   << "repeat(1.25) should wrap to 0.25 (red)";
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::repeat,          "vec2(1.75, 0.5)", "repeat_1_75"), Texel::green) << "repeat(1.75) should wrap to 0.75 (green)";

    // mirrored_repeat: 1.25 -> 0.75 (green), 1.75 -> 0.25 (red).
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::mirrored_repeat, "vec2(1.25, 0.5)", "mirror_1_25"), Texel::green) << "mirror(1.25) should reflect to 0.75 (green)";
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::mirrored_repeat, "vec2(1.75, 0.5)", "mirror_1_75"), Texel::red)   << "mirror(1.75) should reflect to 0.25 (red)";
}

// Full-image sampler ports of the agfx tests SamplerFilterNearest /
// SamplerFilterLinear and SamplerAddressModeRepeat / MirroredRepeat /
// ClampToEdge. agfx writes its output from a compute shader; here a fullscreen
// fragment pass samples the source at uv = IMAGE_POSITION / target size (image
// space, row 0 = image top), so the whole output image is the sampled result
// (doc/plans/graphics_tests_agfx_port.md 2.3). Sources are seeded by
// copy_from_buffer with the CPU pattern's rows as texel rows (row 0 = v = 0),
// so on every backend image row r of the output samples texel row
// floor(v * height) of the pattern, and the CPU model below is the shader's own
// arithmetic. Every output texel is checked against it, then the image against
// its golden.

namespace {

// 16x16 filter source: red ramps with x, green with y, blue is a checker, so a
// nearest magnification shows sharp 4x4 blocks and a linear one blends both the
// ramps and the checker.
constexpr int c_filter_source_size = 16;
constexpr int c_filter_output_size = 64;

[[nodiscard]] auto filter_source_texel(const int x, const int y) -> std::array<uint8_t, 4>
{
    return std::array<uint8_t, 4>{
        static_cast<uint8_t>((x * 16) + 8),
        static_cast<uint8_t>((y * 16) + 8),
        static_cast<uint8_t>((((x + y) % 2) == 0) ? 40 : 200),
        255u
    };
}

// 4x4 address-mode source: every texel a distinct color (red by column, green
// by row, blue by parity).
constexpr int c_address_source_size = 4;
constexpr int c_address_output_size = 96;

[[nodiscard]] auto address_source_texel(const int x, const int y) -> std::array<uint8_t, 4>
{
    return std::array<uint8_t, 4>{
        static_cast<uint8_t>(40 + (x * 60)),
        static_cast<uint8_t>(40 + (y * 60)),
        static_cast<uint8_t>((((x + y) % 2) == 0) ? 90 : 220),
        255u
    };
}

[[nodiscard]] auto make_pattern(const int size, const std::function<std::array<uint8_t, 4>(int, int)>& texel) -> std::vector<uint8_t>
{
    std::vector<uint8_t> pattern(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::array<uint8_t, 4> value = texel(x, y);
            const std::size_t            index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(size)) + static_cast<std::size_t>(x)) * 4u;
            for (std::size_t c = 0; c < 4u; ++c) {
                pattern[index + c] = value[c];
            }
        }
    }
    return pattern;
}

// Texel index an address mode resolves for an unwrapped nearest index i of a
// size-texel axis.
enum class Address_mode : unsigned int { repeat, mirrored_repeat, clamp_to_edge };

[[nodiscard]] auto wrap_index(const int i, const int size, const Address_mode mode) -> int
{
    switch (mode) {
        case Address_mode::repeat: {
            return ((i % size) + size) % size;
        }
        case Address_mode::mirrored_repeat: {
            const int period = 2 * size;
            const int m      = ((i % period) + period) % period;
            return (m < size) ? m : ((period - 1) - m);
        }
        case Address_mode::clamp_to_edge:
        default: {
            return std::clamp(i, 0, size - 1);
        }
    }
}

constexpr const char* c_image_uv_fragment_source = R"glsl(
void main()
{
    vec2 uv = IMAGE_POSITION / vec2(float(TARGET_WIDTH), float(TARGET_HEIGHT));
    out_color = texture(s_texture, (uv * UV_SCALE) + vec2(UV_OFFSET));
}
)glsl";

} // namespace

class Sampler_mode_test : public Gpu_test
{
protected:
    // Seed a size x size RGBA8 source with pattern, sample it over the whole
    // output (uv * uv_scale + uv_offset) with the given sampler, and return the
    // output in image rows.
    [[nodiscard]] auto sample_pattern(
        const std::vector<uint8_t>&                pattern,
        const int                                  source_size,
        const int                                  output_size,
        const erhe::graphics::Sampler_create_info& sampler_create_info,
        const char*                                uv_scale,
        const char*                                uv_offset,
        const char*                                golden_name
    ) -> std::vector<uint8_t>
    {
        const std::shared_ptr<erhe::graphics::Texture> source =
            make_sampled_texture(erhe::graphics::Texture_type::texture_2d, source_size, source_size, 1, 0, "sampler mode source");
        seed_subresource_rgba8(*source, 0, 0, pattern);

        const erhe::graphics::Sampler           sampler{device(), sampler_create_info};
        const erhe::graphics::Bind_group_layout layout = make_sampler_layout(device(), "sampler mode layout");
        const std::array<Sampled_image, 1>      images{ Sampled_image{ .binding_point = 0, .texture = source.get(), .sampler = &sampler } };

        const std::shared_ptr<erhe::graphics::Texture> output = render_fullscreen_pass(
            layout,
            c_image_uv_fragment_source,
            { { "UV_SCALE", uv_scale }, { "UV_OFFSET", uv_offset } },
            images,
            output_size,
            output_size
        );
        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        EXPECT_EQ(pixels.size(), static_cast<std::size_t>(output_size) * static_cast<std::size_t>(output_size) * 4u);
        expect_image_matches_golden(golden_name, output_size, output_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
        return memory_rows_to_image_rows(pixels, static_cast<std::size_t>(output_size) * 4u, output_size);
    }

    [[nodiscard]] static auto make_sampler_info(
        const erhe::graphics::Filter               filter,
        const erhe::graphics::Sampler_address_mode address_mode,
        const char*                                name
    ) -> erhe::graphics::Sampler_create_info
    {
        return erhe::graphics::Sampler_create_info{
            .min_filter   = filter,
            .mag_filter   = filter,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = { address_mode, address_mode, address_mode },
            .debug_label  = erhe::utility::Debug_label{name}
        };
    }

    // uv over [-1, 2] on both axes: output pixel p samples unwrapped texel
    // index floor(((p + 0.5) / 96 * 3 - 1) * 4) = floor(p / 8) - 4 (never on a
    // texel boundary), wrapped by the mode under test.
    void check_address_mode(const erhe::graphics::Sampler_address_mode address_mode, const Address_mode model, const char* golden_name)
    {
        const std::vector<uint8_t> pattern = make_pattern(c_address_source_size, address_source_texel);
        const std::vector<uint8_t> image   = sample_pattern(
            pattern,
            c_address_source_size,
            c_address_output_size,
            make_sampler_info(erhe::graphics::Filter::nearest, address_mode, golden_name),
            "3.0",
            "-1.0",
            golden_name
        );

        const int texel_pixels = c_address_output_size / (3 * c_address_source_size); // 8
        const std::vector<uint8_t> expected = make_pattern(
            c_address_output_size,
            [&](const int x, const int y) {
                const int i = wrap_index((x / texel_pixels) - c_address_source_size, c_address_source_size, model);
                const int j = wrap_index((y / texel_pixels) - c_address_source_size, c_address_source_size, model);
                return address_source_texel(i, j);
            }
        );
        expect_rgba8_near(image, expected, c_address_output_size, c_address_output_size, 0, golden_name);
    }
};

// agfx SamplerFilterNearest: 16x16 source magnified 4x to 64x64 with nearest
// filtering; output pixel (x, y) is exactly source texel (x / 4, y / 4).
TEST_F(Sampler_mode_test, filter_nearest)
{
    const std::vector<uint8_t> pattern = make_pattern(c_filter_source_size, filter_source_texel);
    const std::vector<uint8_t> image   = sample_pattern(
        pattern,
        c_filter_source_size,
        c_filter_output_size,
        make_sampler_info(erhe::graphics::Filter::nearest, erhe::graphics::Sampler_address_mode::clamp_to_edge, "filter nearest"),
        "1.0",
        "0.0",
        "sampler_filter_nearest"
    );
    const int scale = c_filter_output_size / c_filter_source_size;
    const std::vector<uint8_t> expected = make_pattern(
        c_filter_output_size,
        [&](const int x, const int y) { return filter_source_texel(x / scale, y / scale); }
    );
    expect_rgba8_near(image, expected, c_filter_output_size, c_filter_output_size, 0, "filter nearest");
}

// agfx SamplerFilterLinear: the same source and magnification with linear
// filtering and clamp_to_edge. Output pixel x samples texel coordinate
// t = (x + 0.5) / 4 - 0.5, so the bilinear weights are multiples of 1/8 (exact
// at the minimum 4-bit sub-texel precision); the CPU bilinear reference is
// matched within +-2 per channel.
TEST_F(Sampler_mode_test, filter_linear)
{
    const std::vector<uint8_t> pattern = make_pattern(c_filter_source_size, filter_source_texel);
    const std::vector<uint8_t> image   = sample_pattern(
        pattern,
        c_filter_source_size,
        c_filter_output_size,
        make_sampler_info(erhe::graphics::Filter::linear, erhe::graphics::Sampler_address_mode::clamp_to_edge, "filter linear"),
        "1.0",
        "0.0",
        "sampler_filter_linear"
    );
    const float scale = static_cast<float>(c_filter_output_size) / static_cast<float>(c_filter_source_size);
    const std::vector<uint8_t> expected = make_pattern(
        c_filter_output_size,
        [&](const int x, const int y) {
            const float tx = ((static_cast<float>(x) + 0.5f) / scale) - 0.5f;
            const float ty = ((static_cast<float>(y) + 0.5f) / scale) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(tx));
            const int   y0 = static_cast<int>(std::floor(ty));
            const float fx = tx - static_cast<float>(x0);
            const float fy = ty - static_cast<float>(y0);
            const int   last = c_filter_source_size - 1;
            const std::array<uint8_t, 4> t00 = filter_source_texel(std::clamp(x0,     0, last), std::clamp(y0,     0, last));
            const std::array<uint8_t, 4> t10 = filter_source_texel(std::clamp(x0 + 1, 0, last), std::clamp(y0,     0, last));
            const std::array<uint8_t, 4> t01 = filter_source_texel(std::clamp(x0,     0, last), std::clamp(y0 + 1, 0, last));
            const std::array<uint8_t, 4> t11 = filter_source_texel(std::clamp(x0 + 1, 0, last), std::clamp(y0 + 1, 0, last));
            std::array<uint8_t, 4> result{};
            for (std::size_t c = 0; c < 4u; ++c) {
                const float top    = (static_cast<float>(t00[c]) * (1.0f - fx)) + (static_cast<float>(t10[c]) * fx);
                const float bottom = (static_cast<float>(t01[c]) * (1.0f - fx)) + (static_cast<float>(t11[c]) * fx);
                result[c] = static_cast<uint8_t>(std::lround((top * (1.0f - fy)) + (bottom * fy)));
            }
            return result;
        }
    );
    expect_rgba8_near(image, expected, c_filter_output_size, c_filter_output_size, 2, "filter linear");
}

// agfx SamplerAddressModeRepeat: uv over [-1, 2] tiles the 4x4 source 3x3.
TEST_F(Sampler_mode_test, address_mode_repeat)
{
    check_address_mode(erhe::graphics::Sampler_address_mode::repeat, Address_mode::repeat, "sampler_address_mode_repeat");
}

// agfx SamplerAddressModeMirroredRepeat: uv over [-1, 2]; the tiles left of and
// right of (above and below) the centre are mirror images of it.
TEST_F(Sampler_mode_test, address_mode_mirrored_repeat)
{
    check_address_mode(erhe::graphics::Sampler_address_mode::mirrored_repeat, Address_mode::mirrored_repeat, "sampler_address_mode_mirrored_repeat");
}

// agfx SamplerAddressModeClampToEdge: uv over [-1, 2]; outside the centre tile
// the edge texels stretch out to the image border.
TEST_F(Sampler_mode_test, address_mode_clamp_to_edge)
{
    check_address_mode(erhe::graphics::Sampler_address_mode::clamp_to_edge, Address_mode::clamp_to_edge, "sampler_address_mode_clamp_to_edge");
}

} // namespace erhe::graphics::test
