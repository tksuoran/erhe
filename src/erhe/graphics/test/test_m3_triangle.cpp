#include "gpu_test_fixture.hpp"

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
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// No #version and no vertex-input declarations: erhe injects #version, and
// with no_vertex_input = true it injects no vertex attributes, so the triangle
// is emitted from gl_VertexID. erhe injects the out_color declaration from
// fragment_outputs. The triangle covers the half-plane x + y < 0 in clip space
// (vertices at three corners), i.e. roughly half the target.
constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

} // namespace

// Milestone 3: draw a single solid-green triangle into an offscreen target via
// inline GLSL (no vertex buffer, no descriptors), read it back, and assert
// partial coverage: every texel is either green (triangle) or black (clear),
// with the green fraction near one half.
TEST_F(Gpu_test, triangle_draw_coverage)
{
    constexpr int width  = 32;
    constexpr int height = 32;

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    // Empty bind group layout: the shader uses no UBO/SSBO/sampler, so the
    // pipeline statically uses no descriptor set (no VUID-vkCmdDraw-None-08600).
    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"M3 empty layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "m3_triangle",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "M3 triangle shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    // Offscreen color target, cleared to black.
    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"M3 triangle"};

    // Build the pipeline directly (no Render_pipeline_state global registry).
    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly             = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization              = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout          = &empty_layout;
    pipeline_create_info.base.color_blend                = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                   = &shader_stages;
    pipeline_create_info.vertex_input                    = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "M3 pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    int green_count = 0;
    int clear_count = 0;
    int other_count = 0;
    for (int i = 0; i < width * height; ++i) {
        const int r = pixels[(static_cast<std::size_t>(i) * 4u) + 0];
        const int g = pixels[(static_cast<std::size_t>(i) * 4u) + 1];
        const int b = pixels[(static_cast<std::size_t>(i) * 4u) + 2];
        if ((g >= 250) && (r <= 5) && (b <= 5)) {
            ++green_count;
        } else if ((r <= 5) && (g <= 5) && (b <= 5)) {
            ++clear_count;
        } else {
            ++other_count;
        }
    }

    const int total = width * height;
    EXPECT_EQ(other_count, 0) << "every texel should be exactly green or black";
    EXPECT_GT(green_count, 0) << "triangle produced no covered texels";
    EXPECT_GT(clear_count, 0) << "triangle covered the whole target";
    // A corner-to-corner triangle covers about half the target.
    EXPECT_GE(green_count, (total * 30) / 100) << "coverage too low";
    EXPECT_LE(green_count, (total * 70) / 100) << "coverage too high";
}


// -----------------------------------------------------------------------------
// agfx ports: DrawTriangle, Viewport, ScissorRect.
//
// Regions are stated in image coordinates: x from the left, y from the image
// top. NDC +y is up on every backend (Vulkan through the negative-height
// viewport the render command encoder applies), and viewport / scissor
// rectangles are given in framebuffer rows, which run in texture-memory order:
// the image top is row 0 for a top_left texture origin (Vulkan, Metal) and the
// last row for bottom_left (OpenGL). Image_rect::to_framebuffer_y converts
// with the device's coordinate conventions, like application code must, so
// every backend renders the same image and one golden serves all of them.
// -----------------------------------------------------------------------------

namespace {

constexpr const char* c_rgb_triangle_vertex_source = R"glsl(
layout(location = 0) out vec4 v_color;
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-0.75, -0.75),
        vec2( 0.75, -0.75),
        vec2( 0.0,   0.75)
    );
    vec4 colors[3] = vec4[3](
        vec4(1.0, 0.0, 0.0, 1.0),
        vec4(0.0, 1.0, 0.0, 1.0),
        vec4(0.0, 0.0, 1.0, 1.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_color     = colors[gl_VertexID];
}
)glsl";

// The milestone-3 half-plane triangle (x + y < 0 in NDC), green.
constexpr const char* c_half_plane_vertex_source = R"glsl(
layout(location = 0) out vec4 v_color;
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_color     = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

// Oversized triangle covering the whole viewport, green.
constexpr const char* c_fullscreen_vertex_source = R"glsl(
layout(location = 0) out vec4 v_color;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_color     = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

constexpr const char* c_color_fragment_source = R"glsl(
layout(location = 0) in vec4 v_color;
void main()
{
    out_color = v_color;
}
)glsl";

class Image_rect
{
public:
    int x;
    int y; // from the image top
    int width;
    int height;

    [[nodiscard]] auto contains(const int px, const int py) const -> bool
    {
        return (px >= x) && (px < (x + width)) && (py >= y) && (py < (y + height));
    }
};

} // namespace

class Triangle_region_test : public Gpu_test
{
protected:
    [[nodiscard]] auto row0_is_top() -> bool
    {
        return device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left;
    }

    // Framebuffer row of the rectangle's first row for set_viewport_rect /
    // set_scissor_rect.
    [[nodiscard]] auto to_framebuffer_y(const Image_rect& rect, const int target_height) -> int
    {
        return row0_is_top() ? rect.y : (target_height - rect.y - rect.height);
    }

    // RGB of the texel at image coordinates (x, y from the image top).
    [[nodiscard]] auto image_pixel(const std::vector<uint8_t>& pixels, const int size, const int x, const int y) -> std::array<int, 3>
    {
        const int         row   = row0_is_top() ? y : (size - 1 - y);
        const std::size_t index = ((static_cast<std::size_t>(row) * static_cast<std::size_t>(size)) + static_cast<std::size_t>(x)) * 4u;
        return { pixels[index + 0u], pixels[index + 1u], pixels[index + 2u] };
    }

    [[nodiscard]] auto is_green(const std::array<int, 3>& rgb) -> bool
    {
        return (rgb[0] <= 5) && (rgb[1] >= 250) && (rgb[2] <= 5);
    }

    [[nodiscard]] auto is_black(const std::array<int, 3>& rgb) -> bool
    {
        return (rgb[0] <= 5) && (rgb[1] <= 5) && (rgb[2] <= 5);
    }

    // One triangle (three vertices from gl_VertexID) into a size x size target
    // cleared to black, with the given viewport and scissor rectangles.
    [[nodiscard]] auto render_triangle(
        const char*       vertex_source,
        const int         size,
        const Image_rect& viewport,
        const Image_rect& scissor
    ) -> std::vector<uint8_t>
    {
        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(size, size);

        const erhe::graphics::Bind_group_layout empty_layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"triangle region empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };
        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "triangle_region",
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{vertex_source}           },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_color_fragment_source} }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "triangle region shader failed to compile/link";
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
        descriptor.render_target_width  = size;
        descriptor.render_target_height = size;
        descriptor.debug_label = erhe::utility::Debug_label{"triangle region"};

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
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
            ADD_FAILURE() << "triangle region pipeline is not valid";
            return {};
        }

        const int viewport_y = to_framebuffer_y(viewport, size);
        const int scissor_y  = to_framebuffer_y(scissor, size);
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(viewport.x, viewport_y, viewport.width, viewport.height);
                encoder.set_scissor_rect (scissor.x,  scissor_y,  scissor.width,  scissor.height);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );

        return read_texture_rgba8(*color_target);
    }
};

// agfx DrawTriangle: a triangle with red / green / blue vertices (bottom-left
// / bottom-right / top) and interpolated color, over black. Corners stay
// black, texels near each vertex are dominated by that vertex's color and the
// centroid mixes all three.
TEST_F(Triangle_region_test, draw_triangle)
{
    constexpr int size = 64;
    const Image_rect full{ 0, 0, size, size };
    const std::vector<uint8_t> pixels = render_triangle(c_rgb_triangle_vertex_source, size, full, full);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);

    EXPECT_TRUE(is_black(image_pixel(pixels, size, 0,        0       ))) << "top-left corner";
    EXPECT_TRUE(is_black(image_pixel(pixels, size, size - 1, 0       ))) << "top-right corner";
    EXPECT_TRUE(is_black(image_pixel(pixels, size, 0,        size - 1))) << "bottom-left corner";
    EXPECT_TRUE(is_black(image_pixel(pixels, size, size - 1, size - 1))) << "bottom-right corner";

    // Vertices are at image (8, 56), (56, 56) and (32, 8).
    const std::array<int, 3> near_red   = image_pixel(pixels, size, 11, 54);
    const std::array<int, 3> near_green = image_pixel(pixels, size, 52, 54);
    const std::array<int, 3> near_blue  = image_pixel(pixels, size, 32, 10);
    EXPECT_TRUE((near_red[0]   > 200) && (near_red[1]   < 40) && (near_red[2]   < 40)) << "near the red vertex";
    EXPECT_TRUE((near_green[1] > 200) && (near_green[0] < 40) && (near_green[2] < 40)) << "near the green vertex";
    EXPECT_TRUE((near_blue[2]  > 200) && (near_blue[0]  < 40) && (near_blue[1]  < 40)) << "near the blue vertex";

    // Centroid at NDC (0, -0.25) = image (32, 40): about a third of each.
    const std::array<int, 3> centroid = image_pixel(pixels, size, 32, 40);
    for (std::size_t c = 0; c < 3; ++c) {
        EXPECT_GE(centroid[c], 50)  << "centroid channel " << c;
        EXPECT_LE(centroid[c], 130) << "centroid channel " << c;
    }

    expect_image_matches_golden("triangle_rgb", size, size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

// agfx Viewport: the viewport is the bottom-right quadrant of the image, the
// scissor the whole target. The half-plane triangle (x + y < 0 in NDC) lands
// in the lower-left half of that quadrant; the other three quadrants stay
// black.
TEST_F(Triangle_region_test, viewport)
{
    constexpr int size = 64;
    const Image_rect full    { 0,        0,        size,     size     };
    const Image_rect quadrant{ size / 2, size / 2, size / 2, size / 2 };
    const std::vector<uint8_t> pixels = render_triangle(c_half_plane_vertex_source, size, quadrant, full);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);

    int green_inside   = 0;
    int other_inside   = 0;
    int lit_outside    = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::array<int, 3> rgb = image_pixel(pixels, size, x, y);
            if (quadrant.contains(x, y)) {
                if (is_green(rgb)) {
                    ++green_inside;
                } else if (!is_black(rgb)) {
                    ++other_inside;
                }
            } else if (!is_black(rgb)) {
                ++lit_outside;
            }
        }
    }
    EXPECT_EQ(lit_outside, 0) << "texels outside the viewport were written";
    EXPECT_EQ(other_inside, 0) << "texels inside the viewport are neither green nor black";
    // 32 x 32 texels; 496 centres lie strictly below the diagonal, the 32 on
    // it follow the rasterizer's tie-breaking rule.
    EXPECT_GE(green_inside, 496);
    EXPECT_LE(green_inside, 528);
    EXPECT_TRUE(is_green(image_pixel(pixels, size, quadrant.x,                      quadrant.y + quadrant.height - 1))) << "quadrant bottom-left";
    EXPECT_TRUE(is_black(image_pixel(pixels, size, quadrant.x + quadrant.width - 1, quadrant.y                      ))) << "quadrant top-right";

    expect_image_matches_golden("viewport_quadrant", size, size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

// agfx ScissorRect: a fullscreen triangle with the full viewport and an
// off-centre 64 x 64 scissor at image (16, 24) of a 128 x 128 target. Exactly
// the scissor rectangle is green.
TEST_F(Triangle_region_test, scissor_rect)
{
    constexpr int size = 128;
    const Image_rect full   { 0,  0,  size, size };
    const Image_rect scissor{ 16, 24, 64,   64   };
    const std::vector<uint8_t> pixels = render_triangle(c_fullscreen_vertex_source, size, full, scissor);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);

    int wrong_inside  = 0;
    int wrong_outside = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::array<int, 3> rgb = image_pixel(pixels, size, x, y);
            if (scissor.contains(x, y)) {
                if (!is_green(rgb)) {
                    ++wrong_inside;
                }
            } else if (!is_black(rgb)) {
                ++wrong_outside;
            }
        }
    }
    EXPECT_EQ(wrong_inside,  0) << "texels inside the scissor rectangle are not green";
    EXPECT_EQ(wrong_outside, 0) << "texels outside the scissor rectangle were written";

    expect_image_matches_golden("scissor_rect", size, size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

} // namespace erhe::graphics::test
