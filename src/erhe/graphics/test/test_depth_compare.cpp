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

// Depth compare operations, depth clear value and depth write enable, ported
// from the agfx tests DrawDepthTest<Op>, DrawDepthClearValue and
// DrawDepthWrite<Enabled|Disabled>.
//
// Every scene is built from three shapes emitted from gl_VertexID (six
// vertices per quad, no vertex input):
//   - floor   (vertices  0..5):  the whole target, window depth 0.5, gray
//   - columns (vertices  6..23): three columns (left / middle / right thirds)
//                                over the band y in [-0.5, 0.5] NDC, window
//                                depths 0.25 / 0.5 / 0.75, red / green / blue
//   - overlay (vertices 24..29): the same band across the full width, window
//                                depth 0.625, yellow
//
// Depths are stated as window depths (the value the depth test compares and
// the depth attachment stores) and converted to NDC z from the device's
// native_depth_range, so the tests hold on a zero_to_one device (Vulkan,
// Metal, OpenGL with clip control) and a negative_one_to_one one alike. The
// compare ops are used as given: reverse-Z is a projection convention and
// plays no part here, the depth clear value is set explicitly per scene.
// Every chosen depth converts exactly in both ranges, so the equal cases
// compare identical values.
//
// The scenes are symmetric about the horizontal centre line, so the analytic
// region checks do not depend on the texture origin.

namespace erhe::graphics::test {

namespace {

constexpr int c_size = 64;

constexpr float c_floor_depth   = 0.5f;
constexpr float c_left_depth    = 0.25f;
constexpr float c_middle_depth  = 0.5f;
constexpr float c_right_depth   = 0.75f;
constexpr float c_overlay_depth = 0.625f;

constexpr std::uintptr_t c_floor_first    = 0;
constexpr std::uintptr_t c_floor_count    = 6;
constexpr std::uintptr_t c_columns_first  = 6;
constexpr std::uintptr_t c_columns_count  = 18;
constexpr std::uintptr_t c_overlay_first  = 24;
constexpr std::uintptr_t c_overlay_count  = 6;

constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) flat out vec4 v_color;
void main()
{
    vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    int   quad   = gl_VertexID / 6;
    int   corner = gl_VertexID - (quad * 6);
    vec2  lo;
    vec2  hi;
    float z;
    vec4  color;
    if (quad == 0) {
        lo    = vec2(-1.0, -1.0);
        hi    = vec2( 1.0,  1.0);
        z     = FLOOR_Z;
        color = vec4(0.5, 0.5, 0.5, 1.0);
    } else if (quad <= 3) {
        int column = quad - 1;
        lo = vec2(-1.0 + ((2.0 * float(column    )) / 3.0), -0.5);
        hi = vec2(-1.0 + ((2.0 * float(column + 1)) / 3.0),  0.5);
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
    } else {
        lo    = vec2(-1.0, -0.5);
        hi    = vec2( 1.0,  0.5);
        z     = OVERLAY_Z;
        color = vec4(1.0, 1.0, 0.0, 1.0);
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

enum class Shape : unsigned int
{
    floor,
    columns,
    overlay
};

enum class Depth_write : unsigned int
{
    disabled,
    enabled
};

enum class Color : unsigned int
{
    black,
    gray,
    red,
    green,
    blue,
    yellow,
    other
};

[[nodiscard]] auto c_str(const Color color) -> const char*
{
    switch (color) {
        case Color::black:  return "black";
        case Color::gray:   return "gray";
        case Color::red:    return "red";
        case Color::green:  return "green";
        case Color::blue:   return "blue";
        case Color::yellow: return "yellow";
        default:            return "other";
    }
}

class Scene_draw
{
public:
    Shape                             shape;
    erhe::graphics::Compare_operation compare_op;
    Depth_write                       depth_write;
};

class Depth_scene
{
public:
    float                   clear_depth{1.0f};
    std::vector<Scene_draw> draws;
};

[[nodiscard]] auto classify(const std::vector<uint8_t>& pixels, const int x, const int y) -> Color
{
    const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size)) + static_cast<std::size_t>(x)) * 4u;
    const int r = pixels[index + 0u];
    const int g = pixels[index + 1u];
    const int b = pixels[index + 2u];
    auto is_near = [](const int value, const int expected) -> bool {
        return (value >= (expected - 3)) && (value <= (expected + 3));
    };
    if (is_near(r, 0) && is_near(g, 0) && is_near(b, 0)) { return Color::black;  }
    if (is_near(r, 128) && is_near(g, 128) && is_near(b, 128)) { return Color::gray;   }
    if (is_near(r, 255) && is_near(g, 0) && is_near(b, 0)) { return Color::red;    }
    if (is_near(r, 0) && is_near(g, 255) && is_near(b, 0)) { return Color::green;  }
    if (is_near(r, 0) && is_near(g, 0) && is_near(b, 255)) { return Color::blue;   }
    if (is_near(r, 255) && is_near(g, 255) && is_near(b, 0)) { return Color::yellow; }
    return Color::other;
}

// Every texel of the inclusive rectangle must have the expected color.
void expect_region(
    const std::vector<uint8_t>& pixels,
    const int                   x0,
    const int                   x1,
    const int                   y0,
    const int                   y1,
    const Color                 expected,
    const char*                 label
)
{
    int   mismatches = 0;
    Color first_seen = expected;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Color color = classify(pixels, x, y);
            if (color != expected) {
                if (mismatches == 0) {
                    first_seen = color;
                }
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0)
        << label << ": expected " << c_str(expected) << ", " << mismatches
        << " texels differ (first seen: " << c_str(first_seen) << ")";
}

// Columns span x in [0, 21.33), [21.33, 42.67), [42.67, 64); the band spans
// rows [16, 48). The boundary columns 21 and 42 are left out of the checks.
void expect_columns(const std::vector<uint8_t>& pixels, const Color left, const Color middle, const Color right)
{
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);
    expect_region(pixels,  0, 20, 16, 47, left,   "left column");
    expect_region(pixels, 22, 41, 16, 47, middle, "middle column");
    expect_region(pixels, 43, 63, 16, 47, right,  "right column");
}

// Everything outside the band: rows [0, 16) and [48, 64).
void expect_outside_band(const std::vector<uint8_t>& pixels, const Color expected)
{
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);
    expect_region(pixels, 0, c_size - 1,  0, 15,         expected, "above the band");
    expect_region(pixels, 0, c_size - 1, 48, c_size - 1, expected, "below the band");
}

} // namespace

class Depth_compare_test : public Gpu_test
{
protected:
    // NDC z that lands on the given window depth for this device's native
    // depth range (the viewport depth range is the default [0, 1]).
    [[nodiscard]] auto ndc_z(const float window_depth) -> std::string
    {
        const bool zero_to_one = device().get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one;
        const float z = zero_to_one ? window_depth : ((2.0f * window_depth) - 1.0f);
        return std::to_string(z);
    }

    // Clear color black, clear depth scene.clear_depth, then the scene's draws
    // in order, each with its own pipeline, all in one render pass.
    [[nodiscard]] auto render_depth_scene(const Depth_scene& scene) -> std::vector<uint8_t>
    {
        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);
        const std::shared_ptr<erhe::graphics::Texture> depth_target = std::make_shared<erhe::graphics::Texture>(
            device(),
            erhe::graphics::Texture_create_info{
                .device      = device(),
                .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_d32_sfloat,
                .width       = c_size,
                .height      = c_size,
                .debug_label = erhe::utility::Debug_label{"depth compare depth target"}
            }
        );

        const erhe::graphics::Bind_group_layout empty_layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"depth compare empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "depth_compare",
            .defines          = {
                { "FLOOR_Z",   ndc_z(c_floor_depth)   },
                { "LEFT_Z",    ndc_z(c_left_depth)    },
                { "MIDDLE_Z",  ndc_z(c_middle_depth)  },
                { "RIGHT_Z",   ndc_z(c_right_depth)   },
                { "OVERLAY_Z", ndc_z(c_overlay_depth) }
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
            ADD_FAILURE() << "depth compare shader failed to compile/link";
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
        descriptor.depth_attachment.clear_value[0]    = static_cast<double>(scene.clear_depth);
        descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
        descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Dont_care;
        descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::undefined;
        descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
        descriptor.render_target_width  = c_size;
        descriptor.render_target_height = c_size;
        descriptor.debug_label = erhe::utility::Debug_label{"depth compare"};

        std::vector<std::unique_ptr<erhe::graphics::Render_pipeline>> pipelines;
        for (const Scene_draw& draw : scene.draws) {
            erhe::graphics::Render_pipeline_create_info pipeline_create_info;
            pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
            pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
            pipeline_create_info.base.depth_stencil.depth_test_enable   = true;
            pipeline_create_info.base.depth_stencil.depth_write_enable  = (draw.depth_write == Depth_write::enabled);
            pipeline_create_info.base.depth_stencil.depth_compare_op    = draw.compare_op;
            pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
            pipeline_create_info.base.bind_group_layout                 = &empty_layout;
            pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
            pipeline_create_info.shader_stages                          = &shader_stages;
            pipeline_create_info.vertex_input                           = nullptr;
            pipeline_create_info.set_format_from_render_pass(descriptor);
            pipelines.push_back(std::make_unique<erhe::graphics::Render_pipeline>(device(), pipeline_create_info));
            if (!pipelines.back()->is_valid()) {
                ADD_FAILURE() << "depth compare pipeline is not valid";
                return {};
            }
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, c_size, c_size);
                encoder.set_scissor_rect (0, 0, c_size, c_size);
                encoder.set_bind_group_layout(&empty_layout);
                for (std::size_t i = 0; i < scene.draws.size(); ++i) {
                    encoder.set_render_pipeline(*pipelines[i]);
                    switch (scene.draws[i].shape) {
                        case Shape::floor:   encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, c_floor_first,   c_floor_count);   break;
                        case Shape::columns: encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, c_columns_first, c_columns_count); break;
                        case Shape::overlay: encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, c_overlay_first, c_overlay_count); break;
                    }
                }
            }
        );

        return read_texture_rgba8(*color_target);
    }

    // The compare-op scene: the floor at depth 0.5 with always + writes on,
    // then the columns at 0.25 / 0.5 / 0.75 with the op under test + writes
    // off. A column is visible where (column depth <op> 0.5) holds, the floor
    // shows through elsewhere.
    void check_compare_op(
        const erhe::graphics::Compare_operation compare_op,
        const Color                             left,
        const Color                             middle,
        const Color                             right,
        const char*                             golden_name
    )
    {
        const Depth_scene scene{
            .clear_depth = 1.0f,
            .draws = {
                Scene_draw{ .shape = Shape::floor,   .compare_op = erhe::graphics::Compare_operation::always, .depth_write = Depth_write::enabled  },
                Scene_draw{ .shape = Shape::columns, .compare_op = compare_op,                                .depth_write = Depth_write::disabled }
            }
        };
        const std::vector<uint8_t> pixels = render_depth_scene(scene);
        ASSERT_FALSE(pixels.empty());
        expect_columns(pixels, left, middle, right);
        expect_outside_band(pixels, Color::gray);
        expect_image_matches_golden(golden_name, c_size, c_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
    }
};

// agfx DrawDepthTestNever: no column passes.
TEST_F(Depth_compare_test, compare_never)
{
    check_compare_op(erhe::graphics::Compare_operation::never, Color::gray, Color::gray, Color::gray, "depth_compare_never");
}

// agfx DrawDepthTestLess: only the column at 0.25 passes.
TEST_F(Depth_compare_test, compare_less)
{
    check_compare_op(erhe::graphics::Compare_operation::less, Color::red, Color::gray, Color::gray, "depth_compare_less");
}

// agfx DrawDepthTestEqual: only the column at 0.5 passes.
TEST_F(Depth_compare_test, compare_equal)
{
    check_compare_op(erhe::graphics::Compare_operation::equal, Color::gray, Color::green, Color::gray, "depth_compare_equal");
}

// agfx DrawDepthTestLessEqual: the columns at 0.25 and 0.5 pass.
TEST_F(Depth_compare_test, compare_less_or_equal)
{
    check_compare_op(erhe::graphics::Compare_operation::less_or_equal, Color::red, Color::green, Color::gray, "depth_compare_less_or_equal");
}

// agfx DrawDepthTestGreater: only the column at 0.75 passes.
TEST_F(Depth_compare_test, compare_greater)
{
    check_compare_op(erhe::graphics::Compare_operation::greater, Color::gray, Color::gray, Color::blue, "depth_compare_greater");
}

// agfx DrawDepthTestNotEqual: the columns at 0.25 and 0.75 pass.
TEST_F(Depth_compare_test, compare_not_equal)
{
    check_compare_op(erhe::graphics::Compare_operation::not_equal, Color::red, Color::gray, Color::blue, "depth_compare_not_equal");
}

// agfx DrawDepthTestGreaterEqual: the columns at 0.5 and 0.75 pass.
TEST_F(Depth_compare_test, compare_greater_or_equal)
{
    check_compare_op(erhe::graphics::Compare_operation::greater_or_equal, Color::gray, Color::green, Color::blue, "depth_compare_greater_or_equal");
}

// agfx DrawDepthTestAlways: every column passes.
TEST_F(Depth_compare_test, compare_always)
{
    check_compare_op(erhe::graphics::Compare_operation::always, Color::red, Color::green, Color::blue, "depth_compare_always");
}

// agfx DrawDepthClearValue: no floor; the depth attachment is cleared to
// 0.625 and the columns are drawn with less. The columns at 0.25 and 0.5 pass
// against the cleared value, the one at 0.75 does not; a clear to the default
// 1.0 would pass all three, a clear to 0.0 none.
TEST_F(Depth_compare_test, clear_value)
{
    const Depth_scene scene{
        .clear_depth = c_overlay_depth,
        .draws = {
            Scene_draw{ .shape = Shape::columns, .compare_op = erhe::graphics::Compare_operation::less, .depth_write = Depth_write::disabled }
        }
    };
    const std::vector<uint8_t> pixels = render_depth_scene(scene);
    ASSERT_FALSE(pixels.empty());
    expect_columns(pixels, Color::red, Color::green, Color::black);
    expect_outside_band(pixels, Color::black);
    expect_image_matches_golden("depth_clear_value", c_size, c_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

// agfx DrawDepthWriteEnabled: the columns are drawn with always + writes on
// over a depth cleared to 1.0, then the overlay band at 0.625 with less. The
// stored column depths 0.25 and 0.5 reject the overlay, the stored 0.75 does
// not.
TEST_F(Depth_compare_test, write_enabled)
{
    const Depth_scene scene{
        .clear_depth = 1.0f,
        .draws = {
            Scene_draw{ .shape = Shape::columns, .compare_op = erhe::graphics::Compare_operation::always, .depth_write = Depth_write::enabled  },
            Scene_draw{ .shape = Shape::overlay, .compare_op = erhe::graphics::Compare_operation::less,   .depth_write = Depth_write::disabled }
        }
    };
    const std::vector<uint8_t> pixels = render_depth_scene(scene);
    ASSERT_FALSE(pixels.empty());
    expect_columns(pixels, Color::red, Color::green, Color::yellow);
    expect_outside_band(pixels, Color::black);
    expect_image_matches_golden("depth_write_enabled", c_size, c_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

// agfx DrawDepthWriteDisabled: as write_enabled with the column writes off.
// The depth stays at the cleared 1.0, so the overlay covers every column.
TEST_F(Depth_compare_test, write_disabled)
{
    const Depth_scene scene{
        .clear_depth = 1.0f,
        .draws = {
            Scene_draw{ .shape = Shape::columns, .compare_op = erhe::graphics::Compare_operation::always, .depth_write = Depth_write::disabled },
            Scene_draw{ .shape = Shape::overlay, .compare_op = erhe::graphics::Compare_operation::less,   .depth_write = Depth_write::disabled }
        }
    };
    const std::vector<uint8_t> pixels = render_depth_scene(scene);
    ASSERT_FALSE(pixels.empty());
    expect_columns(pixels, Color::yellow, Color::yellow, Color::yellow);
    expect_outside_band(pixels, Color::black);
    expect_image_matches_golden("depth_write_disabled", c_size, c_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

} // namespace erhe::graphics::test
