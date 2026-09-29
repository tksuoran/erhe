#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
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
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

[[nodiscard]] auto count_green(const std::vector<uint8_t>& pixels) -> int
{
    int n = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        if ((pixels[i + 1] >= 250) && (pixels[i + 0] <= 5) && (pixels[i + 2] <= 5)) {
            ++n;
        }
    }
    return n;
}

} // namespace

// Face culling. The fullscreen triangle has one winding, so it is back-facing
// under exactly one front-face convention: cull_mode_back_cw and
// cull_mode_back_ccw must therefore cull it in exactly one of the two cases,
// while cull_mode_none always draws it. Robust to the actual winding / Y
// orientation.
TEST_F(Gpu_test, rasterization_face_culling)
{
    const std::array<double, 4> black{ 0.0, 0.0, 0.0, 1.0 };
    const char* green = "vec4(0.0, 1.0, 0.0, 1.0)";

    const int g_none = count_green(draw_fullscreen_triangle(
        green, erhe::graphics::Rasterization_state::cull_mode_none, erhe::graphics::Color_blend_state::color_blend_disabled, black));
    const int g_cw = count_green(draw_fullscreen_triangle(
        green, erhe::graphics::Rasterization_state::cull_mode_back_cw, erhe::graphics::Color_blend_state::color_blend_disabled, black));
    const int g_ccw = count_green(draw_fullscreen_triangle(
        green, erhe::graphics::Rasterization_state::cull_mode_back_ccw, erhe::graphics::Color_blend_state::color_blend_disabled, black));

    EXPECT_GT(g_none, 0) << "cull_mode_none should draw the triangle";
    EXPECT_TRUE(((g_cw == 0) && (g_ccw > 0)) || ((g_ccw == 0) && (g_cw > 0)))
        << "exactly one of cull_back_cw / cull_back_ccw should cull the triangle"
        << " (none=" << g_none << " cw=" << g_cw << " ccw=" << g_ccw << ")";
    EXPECT_EQ(g_none, (g_cw > g_ccw) ? g_cw : g_ccw) << "the non-culled cull-back case should match cull_none coverage";
}

// Color write mask. Draw white with green+blue writes disabled over a black
// clear; only red and alpha should be written.
TEST_F(Gpu_test, color_write_mask)
{
    erhe::graphics::Color_blend_state mask_state; // enabled=false; write_mask all-true by default
    mask_state.write_mask.green = false;
    mask_state.write_mask.blue  = false;

    const std::array<double, 4> black{ 0.0, 0.0, 0.0, 1.0 };
    const std::vector<uint8_t> pixels = draw_fullscreen_triangle(
        "vec4(1.0, 1.0, 1.0, 1.0)",
        erhe::graphics::Rasterization_state::cull_mode_none,
        mask_state,
        black
    );

    ASSERT_FALSE(pixels.empty());
    int bad = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        const int r = pixels[i + 0];
        const int g = pixels[i + 1];
        const int b = pixels[i + 2];
        const int a = pixels[i + 3];
        const bool ok = (r >= 253) && (g <= 2) && (b <= 2) && (a >= 253);
        if (!ok) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " texels were not {255,0,0,255}; green/blue writes should have been masked off";
}


// -----------------------------------------------------------------------------
// agfx ports: PipelineCullModeNone/Back/Front, DrawCCW, DrawCW,
// DrawFragmentDiscard.
// -----------------------------------------------------------------------------

namespace {

constexpr int c_winding_size = 64;

// Two triangles with opposite winding, as seen with NDC +y up (the convention
// on every backend; Vulkan through the negative-height viewport): the left one
// (red) is counter-clockwise, the right one (green) clockwise. They are mirror
// images of each other about x = 0, and each lies in its own half of the
// target, so the checks do not depend on the texture origin.
constexpr const char* c_winding_vertex_source = R"glsl(
layout(location = 0) flat out vec4 v_color;
void main()
{
    vec2 positions[6] = vec2[6](
        vec2(-0.9, -0.6), vec2(-0.1, -0.6), vec2(-0.5, 0.6),
        vec2( 0.1, -0.6), vec2( 0.5,  0.6), vec2( 0.9, -0.6)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_color     = (gl_VertexID < 3) ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

constexpr const char* c_flat_color_fragment_source = R"glsl(
layout(location = 0) flat in vec4 v_color;
void main()
{
    out_color = v_color;
}
)glsl";

// Fullscreen triangle that forwards NDC x; the fragment shader discards where
// it exceeds the threshold read from a uniform block.
constexpr const char* c_discard_vertex_source = R"glsl(
layout(location = 0) out float v_ndc_x;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_ndc_x     = positions[gl_VertexID].x;
}
)glsl";

constexpr const char* c_discard_fragment_source = R"glsl(
layout(location = 0) in float v_ndc_x;
void main()
{
    if (v_ndc_x > discard_params.threshold) {
        discard;
    }
    out_color = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

enum class Triangle_visibility : unsigned int
{
    culled,
    drawn
};

class Winding_counts
{
public:
    int red_left   {0}; // red texels in the left half
    int green_right{0}; // green texels in the right half
    int misplaced  {0}; // any other non-black texel
};

[[nodiscard]] auto count_winding(const std::vector<uint8_t>& pixels) -> Winding_counts
{
    Winding_counts counts;
    for (int y = 0; y < c_winding_size; ++y) {
        for (int x = 0; x < c_winding_size; ++x) {
            const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_winding_size)) + static_cast<std::size_t>(x)) * 4u;
            const int  r     = pixels[index + 0u];
            const int  g     = pixels[index + 1u];
            const int  b     = pixels[index + 2u];
            const bool left  = (x < (c_winding_size / 2));
            const bool red   = (r >= 250) && (g <= 5) && (b <= 5);
            const bool green = (r <= 5) && (g >= 250) && (b <= 5);
            const bool black = (r <= 5) && (g <= 5) && (b <= 5);
            if (red && left) {
                ++counts.red_left;
            } else if (green && !left) {
                ++counts.green_right;
            } else if (!black) {
                ++counts.misplaced;
            }
        }
    }
    return counts;
}

} // namespace

class Raster_state_test : public Gpu_test
{
protected:
    // Color target cleared to black, stored and left in transfer_src_optimal
    // for read_texture_rgba8.
    [[nodiscard]] auto make_pass_descriptor(erhe::graphics::Texture& color_target, const char* label) -> erhe::graphics::Render_pass_descriptor
    {
        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = &color_target;
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = color_target.get_width();
        descriptor.render_target_height = color_target.get_height();
        descriptor.debug_label = erhe::utility::Debug_label{label};
        return descriptor;
    }

    [[nodiscard]] auto render_winding(const erhe::graphics::Rasterization_state& rasterization) -> std::vector<uint8_t>
    {
        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_winding_size, c_winding_size);

        const erhe::graphics::Bind_group_layout empty_layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"winding empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };
        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "winding",
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_winding_vertex_source}      },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_flat_color_fragment_source} }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "winding shader failed to compile/link";
            return {};
        }
        erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

        const erhe::graphics::Render_pass_descriptor descriptor = make_pass_descriptor(*color_target, "winding");

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = rasterization;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &empty_layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
        if (!pipeline.is_valid()) {
            ADD_FAILURE() << "winding pipeline is not valid";
            return {};
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, c_winding_size, c_winding_size);
                encoder.set_scissor_rect (0, 0, c_winding_size, c_winding_size);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 6);
            }
        );

        return read_texture_rgba8(*color_target);
    }

    // Renders the two triangles and checks which of them survived: the
    // counter-clockwise (red, left) one and / or the clockwise (green, right)
    // one.
    void check_winding(
        const erhe::graphics::Rasterization_state& rasterization,
        const Triangle_visibility                  ccw_triangle,
        const Triangle_visibility                  cw_triangle,
        const char*                                golden_name
    )
    {
        const std::vector<uint8_t> pixels = render_winding(rasterization);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_winding_size) * static_cast<std::size_t>(c_winding_size) * 4u);
        const Winding_counts counts = count_winding(pixels);
        EXPECT_EQ(counts.misplaced, 0) << "texels that are neither black, left red nor right green";
        if (ccw_triangle == Triangle_visibility::drawn) {
            EXPECT_GT(counts.red_left, 0) << "the counter-clockwise triangle should be drawn";
        } else {
            EXPECT_EQ(counts.red_left, 0) << "the counter-clockwise triangle should be culled";
        }
        if (cw_triangle == Triangle_visibility::drawn) {
            EXPECT_GT(counts.green_right, 0) << "the clockwise triangle should be drawn";
        } else {
            EXPECT_EQ(counts.green_right, 0) << "the clockwise triangle should be culled";
        }
        expect_image_matches_golden(golden_name, c_winding_size, c_winding_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
    }
};

// agfx PipelineCullModeNone: both triangles are drawn.
TEST_F(Raster_state_test, cull_mode_none)
{
    check_winding(erhe::graphics::Rasterization_state::cull_mode_none, Triangle_visibility::drawn, Triangle_visibility::drawn, "cull_mode_none");
}

// agfx PipelineCullModeBack: front face counter-clockwise, back faces culled;
// only the counter-clockwise triangle remains.
TEST_F(Raster_state_test, cull_mode_back)
{
    check_winding(erhe::graphics::Rasterization_state::cull_mode_back_ccw, Triangle_visibility::drawn, Triangle_visibility::culled, "cull_mode_back");
}

// agfx PipelineCullModeFront: front face counter-clockwise, front faces
// culled; only the clockwise triangle remains.
TEST_F(Raster_state_test, cull_mode_front)
{
    check_winding(erhe::graphics::Rasterization_state::cull_mode_front_ccw, Triangle_visibility::culled, Triangle_visibility::drawn, "cull_mode_front");
}

// agfx DrawCCW: front face counter-clockwise with back-face culling; the
// counter-clockwise triangle is the front face and is drawn.
TEST_F(Raster_state_test, front_face_ccw)
{
    check_winding(erhe::graphics::Rasterization_state::cull_mode_back_ccw, Triangle_visibility::drawn, Triangle_visibility::culled, "front_face_ccw");
}

// agfx DrawCW: front face clockwise with back-face culling; the clockwise
// triangle is now the front face and is drawn.
TEST_F(Raster_state_test, front_face_cw)
{
    check_winding(erhe::graphics::Rasterization_state::cull_mode_back_cw, Triangle_visibility::culled, Triangle_visibility::drawn, "front_face_cw");
}

// agfx DrawFragmentDiscard: a fullscreen triangle whose fragment shader
// discards where NDC x exceeds a threshold of 0.5 read from a uniform block.
// On a 64-texel-wide target the texel centres of columns 0..47 lie at NDC
// x < 0.5 and are green, columns 48..63 keep the black clear color.
TEST_F(Raster_state_test, fragment_discard)
{
    constexpr int   size      = 64;
    constexpr float threshold = 0.5f;
    constexpr int   kept      = 48;

    erhe::graphics::Shader_resource ubo_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "discard_params",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::uniform_block
        }
    };
    const erhe::graphics::Shader_resource* threshold_member = ubo_block.add_float("threshold");
    const std::size_t ubo_bytes = ubo_block.get_size_bytes(erhe::graphics::Shader_resource::Layout::std140);
    ASSERT_GE(ubo_bytes, sizeof(float));

    const erhe::graphics::Bind_group_layout layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = erhe::graphics::Binding_type::uniform_buffer,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"discard layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "fragment_discard",
        .interface_blocks = { &ubo_block },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_discard_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_discard_fragment_source} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "fragment discard shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(size, size);
    const erhe::graphics::Render_pass_descriptor descriptor = make_pass_descriptor(*color_target, "fragment discard");

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &layout;
    pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "fragment discard pipeline is not valid";

    const std::shared_ptr<erhe::graphics::Buffer> ubo =
        make_host_buffer(ubo_bytes, erhe::graphics::Buffer_usage::uniform, "discard params");
    {
        const std::span<std::byte> mapped = ubo->map_bytes(0, ubo_bytes);
        std::memset(mapped.data(), 0, ubo_bytes);
        std::memcpy(mapped.data() + threshold_member->get_offset_in_parent(), &threshold, sizeof(threshold));
        ubo->unmap();
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, size, size);
            encoder.set_scissor_rect (0, 0, size, size);
            encoder.set_bind_group_layout(&layout);
            encoder.set_render_pipeline(pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::uniform, ubo.get(), 0, ubo_bytes, 0);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);

    int wrong_kept      = 0;
    int wrong_discarded = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(size)) + static_cast<std::size_t>(x)) * 4u;
            const int r = pixels[index + 0u];
            const int g = pixels[index + 1u];
            const int b = pixels[index + 2u];
            if (x < kept) {
                if (!((r <= 5) && (g >= 250) && (b <= 5))) {
                    ++wrong_kept;
                }
            } else if (!((r <= 5) && (g <= 5) && (b <= 5))) {
                ++wrong_discarded;
            }
        }
    }
    EXPECT_EQ(wrong_kept,      0) << "texels left of NDC x 0.5 should be green";
    EXPECT_EQ(wrong_discarded, 0) << "texels right of NDC x 0.5 should have been discarded";

    expect_image_matches_golden("fragment_discard", size, size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

} // namespace erhe::graphics::test
