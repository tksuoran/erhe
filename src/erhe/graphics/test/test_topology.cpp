#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/vertex_format.hpp"
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
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr int c_size = 16;

// White, no vertex input. Both topology tests emit positions from gl_VertexID.
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(1.0, 1.0, 1.0, 1.0);
}
)glsl";

// Four 1-pixel points placed at exact target pixel centres. gl_PointSize is
// written (via the explicit gl_PerVertex out block) so the rasterizer produces a
// defined single-pixel point.
//
// Each point is emitted from a target pixel coordinate (TARGET_X<i>, TARGET_Y<i>)
// in image-memory space (row 0 == first row returned by read_texture_rgba8) via
// the pixel-centre mapping. For a target of size TARGET_SIZE and target pixel
// (px, py) the NDC that lands on that pixel's centre is:
//     ndc_x =        ((px + 0.5) / TARGET_SIZE) * 2 - 1
//     ndc_y = Y_SIGN ((py + 0.5) / TARGET_SIZE) * 2 - 1)
// Y_SIGN is queried from the device's coordinate-space conventions (texture_origin),
// like all application code must do: it is -1 when read-back row 0 is the image top
// (top_left, Vulkan/Metal) and +1 when row 0 is the image bottom (bottom_left,
// OpenGL), so increasing py always moves toward later read-back rows regardless of
// backend. Mapping to pixel centres (offset +0.5) removes the half-pixel
// rasterization ambiguity, so each point lands on exactly one unambiguous texel.
constexpr const char* c_point_vertex_source = R"glsl(
out gl_PerVertex {
    vec4  gl_Position;
    float gl_PointSize;
};
float pixel_center_ndc(float pixel)
{
    return ((pixel + 0.5) / float(TARGET_SIZE)) * 2.0 - 1.0;
}
void main()
{
    vec2 target_pixels[4] = vec2[4](
        vec2(float(TARGET_X0), float(TARGET_Y0)),
        vec2(float(TARGET_X1), float(TARGET_Y1)),
        vec2(float(TARGET_X2), float(TARGET_Y2)),
        vec2(float(TARGET_X3), float(TARGET_Y3))
    );
    vec2 p = target_pixels[gl_VertexID];
    gl_Position  = vec4(pixel_center_ndc(p.x), float(Y_SIGN) * pixel_center_ndc(p.y), 0.0, 1.0);
    gl_PointSize = 1.0;
}
)glsl";

// A single horizontal segment through y = 0 (NDC), spanning most of the width.
// y = 0 maps to the centre row regardless of Y orientation.
constexpr const char* c_line_vertex_source = R"glsl(
void main()
{
    vec2 positions[2] = vec2[2](
        vec2(-0.9, 0.0),
        vec2( 0.9, 0.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Count texels whose red channel is lit (all draws emit white over a black clear).
auto count_lit_texels(const std::vector<uint8_t>& pixels, const int width, const int height) -> int
{
    int count = 0;
    for (int i = 0; i < width * height; ++i) {
        if (pixels[(static_cast<std::size_t>(i) * 4u) + 0u] > 127u) {
            ++count;
        }
    }
    return count;
}

} // namespace

// Primitive topology: point_list. Draws four 1-pixel points and asserts that
// exactly four texels are lit (one per point), including the Y-orientation
// independent centre texel. Exercises Input_assembly_state::point +
// Primitive_type::point.
TEST_F(Gpu_test, topology_point_list)
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);

    // Target pixel centres (image-memory coordinates: row 0 == first row returned
    // by read_texture_rgba8) for the four points. Chosen interior pixels so the
    // 1-pixel point cannot be clipped and the lit texels are unambiguous.
    class Target_pixel { public: int x; int y; };
    constexpr std::array<Target_pixel, 4> target_pixels{
        Target_pixel{ 8,  8},
        Target_pixel{ 4,  4},
        Target_pixel{12,  4},
        Target_pixel{12, 12}
    };

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"topology point layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    // Respect the device's coordinate-space conventions (like application code):
    // a top_left texture origin puts read-back row 0 at the image top (negate Y),
    // a bottom_left origin puts it at the image bottom (do not negate).
    const erhe::math::Coordinate_conventions& conventions = device().get_info().coordinate_conventions;
    const char* const y_sign =
        (conventions.texture_origin == erhe::math::Texture_origin::top_left) ? "-1.0" : "1.0";

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "topology_point",
        .defines          = {
            { "Y_SIGN",      y_sign                                },
            { "TARGET_SIZE", std::to_string(c_size)                },
            { "TARGET_X0",   std::to_string(target_pixels[0].x)    },
            { "TARGET_Y0",   std::to_string(target_pixels[0].y)    },
            { "TARGET_X1",   std::to_string(target_pixels[1].x)    },
            { "TARGET_Y1",   std::to_string(target_pixels[1].y)    },
            { "TARGET_X2",   std::to_string(target_pixels[2].x)    },
            { "TARGET_Y2",   std::to_string(target_pixels[2].y)    },
            { "TARGET_X3",   std::to_string(target_pixels[3].x)    },
            { "TARGET_Y3",   std::to_string(target_pixels[3].y)    }
        },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_point_vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source}     }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "topology point shader failed to compile/link";
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
    descriptor.render_target_width  = c_size;
    descriptor.render_target_height = c_size;
    descriptor.debug_label = erhe::utility::Debug_label{"topology point"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::point;
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
    ASSERT_TRUE(pipeline.is_valid()) << "topology point pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_size, c_size);
            encoder.set_scissor_rect (0, 0, c_size, c_size);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::point, 0, 4);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);

    // Four 1-pixel points -> exactly four lit texels.
    EXPECT_EQ(count_lit_texels(pixels, c_size, c_size), 4);

    // Each point was placed at the centre of its chosen target pixel, so exactly
    // those four texels (and no others, per the count above) must be lit. This is
    // unambiguous: the pixel-centre mapping removes the half-pixel boundary case.
    auto is_lit = [&](const int x, const int y) -> bool {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size) + static_cast<std::size_t>(x)) * 4u;
        return pixels[index + 0u] > 127u;
    };
    for (const Target_pixel& target : target_pixels) {
        EXPECT_TRUE(is_lit(target.x, target.y))
            << "point at target pixel (" << target.x << ", " << target.y << ") was not lit";
    }
}

// Primitive topology: line_list. Draws one horizontal segment through the centre
// row and asserts coarse coverage along the run while the top and bottom rows stay
// dark (confirming a thin line, not a filled region). Exercises
// Input_assembly_state::line + Primitive_type::line.
TEST_F(Gpu_test, topology_line_list)
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"topology line layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "topology_line",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_line_vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source}    }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "topology line shader failed to compile/link";
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
    descriptor.render_target_width  = c_size;
    descriptor.render_target_height = c_size;
    descriptor.debug_label = erhe::utility::Debug_label{"topology line"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::line;
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
    ASSERT_TRUE(pipeline.is_valid()) << "topology line pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_size, c_size);
            encoder.set_scissor_rect (0, 0, c_size, c_size);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::line, 0, 2);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);

    // The segment spans ~0.9 of the half-width on each side -> ~0.9 * 16 ~= 14 texels.
    // Assert coarse coverage rather than an exact run to stay rasterizer-tolerant.
    const int lit = count_lit_texels(pixels, c_size, c_size);
    EXPECT_GE(lit, 8);
    EXPECT_LE(lit, c_size + 2);

    // All lit texels must lie on (at most) the centre rows: the very top and bottom
    // rows must be dark. This confirms the lit run is a thin horizontal line.
    auto row_is_dark = [&](const int y) -> bool {
        for (int x = 0; x < c_size; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size) + static_cast<std::size_t>(x)) * 4u;
            if (pixels[index + 0u] > 127u) {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(row_is_dark(0));
    EXPECT_TRUE(row_is_dark(c_size - 1));
}


// -----------------------------------------------------------------------------
// agfx ports: DrawPoints, DrawLines, DrawLinesIndexed.
//
// Positions are stated in image coordinates (x from the left, y from the image
// top) at texel centres and converted to NDC with +y up, which is the same on
// every backend, so every backend renders the same image and one golden serves
// all of them. Analytic checks that address a texel convert the image row to
// the read-back row with the device's texture origin.
// -----------------------------------------------------------------------------

namespace {

constexpr int c_port_size = 64;

// Six single-texel points, one per vertex, from gl_VertexID.
constexpr const char* c_six_points_vertex_source = R"glsl(
out gl_PerVertex {
    vec4  gl_Position;
    float gl_PointSize;
};
void main()
{
    vec2 ndc_positions[6] = vec2[6](POINT_NDC_0, POINT_NDC_1, POINT_NDC_2, POINT_NDC_3, POINT_NDC_4, POINT_NDC_5);
    gl_Position  = vec4(ndc_positions[gl_VertexID], 0.0, 1.0);
    gl_PointSize = 1.0;
}
)glsl";

// erhe injects "layout(location = 0) in vec2 a_position;" from the vertex
// format.
constexpr const char* c_buffer_line_vertex_source = R"glsl(
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

class Image_point
{
public:
    int x;
    int y; // from the image top
};

constexpr std::array<Image_point, 6> c_point_positions{
    Image_point{  8,  8 },
    Image_point{ 24, 12 },
    Image_point{ 40, 20 },
    Image_point{ 56, 30 },
    Image_point{ 16, 48 },
    Image_point{ 48, 56 }
};

// The corners of a square outline, at texel centres; the lines are the four
// edges and the diagonal from the image top-left corner to the bottom-right.
constexpr std::array<Image_point, 4> c_square_corners{
    Image_point{  8,  8 },
    Image_point{ 55,  8 },
    Image_point{ 55, 55 },
    Image_point{  8, 55 }
};
constexpr std::array<uint16_t, 10> c_square_line_indices{ 0, 1, 1, 2, 2, 3, 3, 0, 0, 2 };

[[nodiscard]] auto texel_centre_ndc_x(const int x) -> float
{
    return (((static_cast<float>(x) + 0.5f) / static_cast<float>(c_port_size)) * 2.0f) - 1.0f;
}

[[nodiscard]] auto texel_centre_ndc_y(const int y) -> float
{
    return 1.0f - (((static_cast<float>(y) + 0.5f) / static_cast<float>(c_port_size)) * 2.0f);
}

[[nodiscard]] auto glsl_vec2(const float x, const float y) -> std::string
{
    return "vec2(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

enum class Line_indexing : unsigned int
{
    non_indexed,
    indexed
};

} // namespace

class Topology_test : public Gpu_test
{
protected:
    [[nodiscard]] auto row0_is_top() -> bool
    {
        return device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left;
    }

    [[nodiscard]] auto is_lit_at_image(const std::vector<uint8_t>& pixels, const int x, const int y) -> bool
    {
        const int         row   = row0_is_top() ? y : (c_port_size - 1 - y);
        const std::size_t index = ((static_cast<std::size_t>(row) * static_cast<std::size_t>(c_port_size)) + static_cast<std::size_t>(x)) * 4u;
        return pixels[index + 0u] > 127u;
    }

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

    // The square outline plus diagonal as a line_list, either from a vertex
    // buffer of ten expanded vertices or from a vertex buffer of the four
    // corners plus ten uint16 indices. Both feed the rasterizer bit-identical
    // positions.
    [[nodiscard]] auto render_square_lines(const Line_indexing indexing) -> std::vector<uint8_t>
    {
        std::vector<float> positions;
        if (indexing == Line_indexing::indexed) {
            for (const Image_point& corner : c_square_corners) {
                positions.push_back(texel_centre_ndc_x(corner.x));
                positions.push_back(texel_centre_ndc_y(corner.y));
            }
        } else {
            for (const uint16_t index : c_square_line_indices) {
                positions.push_back(texel_centre_ndc_x(c_square_corners[index].x));
                positions.push_back(texel_centre_ndc_y(c_square_corners[index].y));
            }
        }
        const std::size_t vertex_bytes = positions.size() * sizeof(float);
        const std::size_t index_bytes  = sizeof(c_square_line_indices);

        const std::shared_ptr<erhe::graphics::Buffer> vertex_buffer =
            make_host_buffer(vertex_bytes, erhe::graphics::Buffer_usage::vertex, "square line vertices");
        {
            const std::span<std::byte> mapped = vertex_buffer->map_bytes(0, vertex_bytes);
            std::memcpy(mapped.data(), positions.data(), vertex_bytes);
            vertex_buffer->unmap();
        }
        std::shared_ptr<erhe::graphics::Buffer> index_buffer;
        if (indexing == Line_indexing::indexed) {
            index_buffer = make_host_buffer(index_bytes, erhe::graphics::Buffer_usage::index, "square line indices");
            const std::span<std::byte> mapped = index_buffer->map_bytes(0, index_bytes);
            std::memcpy(mapped.data(), c_square_line_indices.data(), index_bytes);
            index_buffer->unmap();
        }

        const erhe::dataformat::Vertex_format vertex_format{
            erhe::dataformat::Vertex_stream{
                0,
                { erhe::dataformat::Vertex_attribute{
                      .format      = erhe::dataformat::Format::format_32_vec2_float,
                      .usage_type  = erhe::dataformat::Vertex_attribute_usage::position,
                      .usage_index = 0
                  } }
            }
        };
        const erhe::graphics::Vertex_input_state vertex_input{
            device(),
            erhe::graphics::Vertex_input_state_data::make(vertex_format)
        };

        const erhe::graphics::Bind_group_layout empty_layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"square lines layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };
        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "square_lines",
            .fragment_outputs = &fragment_outputs,
            .vertex_format    = &vertex_format,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_buffer_line_vertex_source} },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source}    }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "square lines shader failed to compile/link";
            return {};
        }
        erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_port_size, c_port_size);
        const erhe::graphics::Render_pass_descriptor descriptor = make_pass_descriptor(*color_target, "square lines");

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::line;
        pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &empty_layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = &vertex_input;
        pipeline_create_info.vertex_format                          = &vertex_format;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
        if (!pipeline.is_valid()) {
            ADD_FAILURE() << "square lines pipeline is not valid";
            return {};
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, c_port_size, c_port_size);
                encoder.set_scissor_rect (0, 0, c_port_size, c_port_size);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_vertex_buffer(vertex_buffer.get(), 0, 0);
                if (indexing == Line_indexing::indexed) {
                    encoder.set_index_buffer(index_buffer.get());
                    encoder.draw_indexed_primitives(
                        erhe::graphics::Primitive_type::line,
                        c_square_line_indices.size(),
                        erhe::dataformat::Format::format_16_scalar_uint,
                        0
                    );
                } else {
                    encoder.draw_primitives(erhe::graphics::Primitive_type::line, 0, c_square_line_indices.size());
                }
            }
        );

        return read_texture_rgba8(*color_target);
    }

    // Five 47-texel runs (four edges and the diagonal, sharing corners) light
    // about 230 texels; a missing line drops the count below the minimum, a
    // filled square (about 2300) exceeds the maximum. Rows 0..6 and 57..63 lie
    // outside the square and stay dark.
    void check_square_lines(const std::vector<uint8_t>& pixels)
    {
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_port_size) * static_cast<std::size_t>(c_port_size) * 4u);
        const int lit = count_lit_texels(pixels, c_port_size, c_port_size);
        EXPECT_GE(lit, 200) << "fewer lit texels than five lines of 47 texels give";
        EXPECT_LE(lit, 260) << "more lit texels than five thin lines give";
        int lit_outside = 0;
        for (int y = 0; y < c_port_size; ++y) {
            if ((y >= 7) && (y <= 56)) {
                continue;
            }
            for (int x = 0; x < c_port_size; ++x) {
                if (is_lit_at_image(pixels, x, y)) {
                    ++lit_outside;
                }
            }
        }
        EXPECT_EQ(lit_outside, 0) << "texels above or below the square are lit";
        EXPECT_TRUE(is_lit_at_image(pixels, 31,  8)) << "top edge missing at its midpoint";
        EXPECT_TRUE(is_lit_at_image(pixels, 31, 55)) << "bottom edge missing at its midpoint";
        EXPECT_TRUE(is_lit_at_image(pixels,  8, 31)) << "left edge missing at its midpoint";
        EXPECT_TRUE(is_lit_at_image(pixels, 55, 31)) << "right edge missing at its midpoint";
        EXPECT_TRUE(is_lit_at_image(pixels, 31, 31)) << "the diagonal is missing at the centre";
    }
};

// agfx DrawPoints: six single-texel points at distinct texel centres; exactly
// six texels are lit, each at its point.
TEST_F(Topology_test, draw_points)
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_port_size, c_port_size);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"draw points layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    std::vector<std::pair<std::string, std::string>> defines;
    for (std::size_t i = 0; i < c_point_positions.size(); ++i) {
        defines.emplace_back(
            "POINT_NDC_" + std::to_string(i),
            glsl_vec2(texel_centre_ndc_x(c_point_positions[i].x), texel_centre_ndc_y(c_point_positions[i].y))
        );
    }
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "draw_points",
        .defines          = defines,
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_six_points_vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source}          }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "draw points shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    const erhe::graphics::Render_pass_descriptor descriptor = make_pass_descriptor(*color_target, "draw points");

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::point;
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
    ASSERT_TRUE(pipeline.is_valid()) << "draw points pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_port_size, c_port_size);
            encoder.set_scissor_rect (0, 0, c_port_size, c_port_size);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::point, 0, c_point_positions.size());
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_port_size) * static_cast<std::size_t>(c_port_size) * 4u);

    EXPECT_EQ(count_lit_texels(pixels, c_port_size, c_port_size), 6);
    for (const Image_point& point : c_point_positions) {
        EXPECT_TRUE(is_lit_at_image(pixels, point.x, point.y))
            << "point at image texel (" << point.x << ", " << point.y << ") was not lit";
    }

    expect_image_matches_golden("draw_points", c_port_size, c_port_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

// agfx DrawLines: a square outline plus one diagonal as a non-indexed
// line_list from a vertex buffer.
TEST_F(Topology_test, draw_lines)
{
    const std::vector<uint8_t> pixels = render_square_lines(Line_indexing::non_indexed);
    check_square_lines(pixels);
    ASSERT_FALSE(pixels.empty());
    expect_image_matches_golden("draw_lines", c_port_size, c_port_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

// agfx DrawLinesIndexed: the same lines from four corner vertices and ten
// uint16 indices. The image must be identical to the non-indexed draw.
TEST_F(Topology_test, draw_lines_indexed)
{
    const std::vector<uint8_t> pixels = render_square_lines(Line_indexing::indexed);
    check_square_lines(pixels);
    ASSERT_FALSE(pixels.empty());
    const std::vector<uint8_t> non_indexed_pixels = render_square_lines(Line_indexing::non_indexed);
    EXPECT_TRUE(pixels == non_indexed_pixels) << "indexed and non-indexed line lists rasterized differently";
    expect_image_matches_golden("draw_lines_indexed", c_port_size, c_port_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

} // namespace erhe::graphics::test
