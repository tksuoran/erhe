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
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Depth clamp, ported from the agfx tests DrawDepthClamp<Disabled|Enabled>.
//
// Three columns (left / middle / right thirds, band y in [-0.5, 0.5] NDC,
// red / green / blue) are drawn at window depths -0.5 / 0.5 / 1.5 with the
// depth test at always and writes on, over a depth cleared to 0.25. Window
// depths are converted to NDC z from the device's native_depth_range (see
// test_depth_compare.cpp), so the outer columns lie outside the [0, 1] depth
// range on every device. Without depth clamp they are clipped away (black,
// depth keeps the cleared 0.25); with Rasterization_state::depth_clamp_enable
// they are drawn and their depth is clamped to 0.0 and 1.0. Both the color
// and the depth attachment are read back. The scene is symmetric about the
// horizontal centre line, so the checks do not depend on the texture origin.

namespace erhe::graphics::test {

namespace {

constexpr int   c_size        = 64;
constexpr float c_clear_depth = 0.25f;

constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) flat out vec4 v_color;
void main()
{
    vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    int   column = gl_VertexID / 6;
    int   corner = gl_VertexID - (column * 6);
    vec2  lo     = vec2(-1.0 + ((2.0 * float(column    )) / 3.0), -0.5);
    vec2  hi     = vec2(-1.0 + ((2.0 * float(column + 1)) / 3.0),  0.5);
    float z;
    vec4  color;
    if (column == 0) {
        z     = LEFT_Z;
        color = vec4(1.0, 0.0, 0.0, 1.0);
    } else if (column == 1) {
        z     = MIDDLE_Z;
        color = vec4(0.0, 1.0, 0.0, 1.0);
    } else {
        z     = RIGHT_Z;
        color = vec4(0.0, 0.0, 1.0, 1.0);
    }
    gl_Position = vec4(mix(lo, hi, corners[corner]), z, 1.0);
    v_color     = color;
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) flat in vec4 v_color;
void main()
{
    out_color = v_color;
}
)glsl";

enum class Depth_clamp : unsigned int
{
    disabled,
    enabled
};

class Clamp_result
{
public:
    std::vector<uint8_t> pixels;
    std::vector<float>   depths;
};

[[nodiscard]] auto pixel_at(const std::vector<uint8_t>& pixels, const int x, const int y) -> std::array<int, 3>
{
    const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size)) + static_cast<std::size_t>(x)) * 4u;
    return { pixels[index + 0u], pixels[index + 1u], pixels[index + 2u] };
}

[[nodiscard]] auto depth_at(const std::vector<float>& depths, const int x, const int y) -> float
{
    return depths[(static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size)) + static_cast<std::size_t>(x)];
}

// Column sample points (x at the centre of each third) on the centre row of
// the band.
constexpr std::array<int, 3> c_column_x{ 10, 32, 53 };
constexpr int                c_band_y = 32;

} // namespace

class Depth_clamp_test : public Gpu_test
{
protected:
    [[nodiscard]] auto ndc_z(const float window_depth) -> std::string
    {
        const bool zero_to_one = device().get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one;
        const float z = zero_to_one ? window_depth : ((2.0f * window_depth) - 1.0f);
        return std::to_string(z);
    }

    [[nodiscard]] auto render_columns(const Depth_clamp depth_clamp) -> Clamp_result
    {
        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);
        const std::shared_ptr<erhe::graphics::Texture> depth_target = std::make_shared<erhe::graphics::Texture>(
            device(),
            erhe::graphics::Texture_create_info{
                .device      = device(),
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_d32_sfloat,
                .width       = c_size,
                .height      = c_size,
                .debug_label = erhe::utility::Debug_label{"depth clamp depth target"}
            }
        );

        const erhe::graphics::Bind_group_layout empty_layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"depth clamp empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "depth_clamp",
            .defines          = {
                { "LEFT_Z",   ndc_z(-0.5f) },
                { "MIDDLE_Z", ndc_z( 0.5f) },
                { "RIGHT_Z",  ndc_z( 1.5f) }
            },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "depth clamp shader failed to compile/link";
            return {};
        }
        erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = color_target.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.depth_attachment.texture           = depth_target.get();
        descriptor.depth_attachment.clear_value[0]    = static_cast<double>(c_clear_depth);
        descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
        descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Store;
        descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::undefined;
        descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = c_size;
        descriptor.render_target_height = c_size;
        descriptor.debug_label = erhe::utility::Debug_label{"depth clamp"};

        erhe::graphics::Rasterization_state rasterization = erhe::graphics::Rasterization_state::cull_mode_none;
        rasterization.depth_clamp_enable = (depth_clamp == Depth_clamp::enabled);

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = rasterization;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = true;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = true;
        pipeline_create_info.base.depth_stencil.depth_compare_op    = erhe::graphics::Compare_operation::always;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &empty_layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
        if (!pipeline.is_valid()) {
            ADD_FAILURE() << "depth clamp pipeline is not valid";
            return {};
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, c_size, c_size);
                encoder.set_scissor_rect (0, 0, c_size, c_size);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 18);
            }
        );

        Clamp_result result;
        result.pixels = read_texture_rgba8(*color_target);
        result.depths = read_texture_depth32f(*depth_target);
        return result;
    }

    void expect_column(
        const Clamp_result&       result,
        const std::size_t         column,
        const std::array<int, 3>& expected_rgb,
        const float               expected_depth
    )
    {
        const int                x   = c_column_x[column];
        const std::array<int, 3> rgb = pixel_at(result.pixels, x, c_band_y);
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_NEAR(rgb[c], expected_rgb[c], 2) << "column " << column << " channel " << c;
        }
        EXPECT_EQ(depth_at(result.depths, x, c_band_y), expected_depth) << "column " << column << " depth";
    }
};

// agfx DrawDepthClampDisabled: the columns outside [0, 1] are clipped; only
// the middle one is drawn and its depth written.
TEST_F(Depth_clamp_test, clamp_disabled)
{
    const Clamp_result result = render_columns(Depth_clamp::disabled);
    ASSERT_EQ(result.pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);
    ASSERT_EQ(result.depths.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size));
    expect_column(result, 0, {  0,   0, 0}, c_clear_depth);
    expect_column(result, 1, {  0, 255, 0}, 0.5f);
    expect_column(result, 2, {  0,   0, 0}, c_clear_depth);
    expect_image_matches_golden("depth_clamp_disabled", c_size, c_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{result.pixels}));
}

// agfx DrawDepthClampEnabled: all three columns are drawn; the stored depths
// are the clamped 0.0 and 1.0 for the outer columns.
TEST_F(Depth_clamp_test, clamp_enabled)
{
    if (!device().get_info().use_depth_clamp) {
        GTEST_SKIP() << "device does not support depth clamp (Device_info::use_depth_clamp)";
    }
    const Clamp_result result = render_columns(Depth_clamp::enabled);
    ASSERT_EQ(result.pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);
    ASSERT_EQ(result.depths.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size));
    expect_column(result, 0, {255,   0,   0}, 0.0f);
    expect_column(result, 1, {  0, 255,   0}, 0.5f);
    expect_column(result, 2, {  0,   0, 255}, 1.0f);
    expect_image_matches_golden("depth_clamp_enabled", c_size, c_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{result.pixels}));
}

} // namespace erhe::graphics::test
