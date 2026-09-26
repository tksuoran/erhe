// Debug_renderer wide-line width (doc/erhe/renderer.md "Line widths").
//
// A negative Primitive_renderer thickness is a constant screen-space width:
// set_thickness(-N) draws a line N logical pixels wide, and View::pixel_scale
// converts logical pixels to framebuffer pixels. The width must not depend on
// the viewport size or the projection. Each case draws one vertical line
// through Debug_renderer (compute expansion + graphics draw, exactly as the
// editor does) into an offscreen target and counts the lit pixels across the
// line on the middle row.
//
// The line sits at NDC x = 0, which is the pixel boundary W/2 for an even
// target width W, so a ribbon of width N covers exactly the N pixel centers
// W/2 - N/2 .. W/2 + N/2 - 1: the count is exact, not approximate.

#include "gpu_test_fixture.hpp"

#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_renderer/view.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace erhe::renderer::test {

using erhe::graphics::test::Gpu_test;

namespace {

constexpr int c_target_height = 64;

enum class Projection_kind : unsigned int
{
    perspective,
    orthographic
};

class Line_case
{
public:
    int             target_width;
    Projection_kind projection;
    float           fov_y;        // perspective only, radians
    float           pixel_scale;
    float           thickness;    // passed to Primitive_renderer::set_thickness()
};

} // anonymous namespace

class Debug_line_width_gpu_test : public Gpu_test
{
protected:
    void SetUp() override
    {
        Gpu_test::SetUp();

        static bool s_logging_initialized = false;
        if (!s_logging_initialized) {
            erhe::renderer::initialize_logging();
            s_logging_initialized = true;
        }

        ASSERT_TRUE(std::filesystem::exists("res/shaders/compute_before_line.comp"))
            << "Debug_renderer shaders not found from working directory " << std::filesystem::current_path().string();

        m_depth_stencil_format = device().choose_depth_stencil_format(
            erhe::graphics::format_flag_require_depth | erhe::graphics::format_flag_require_stencil,
            1
        );
        if (
            (m_depth_stencil_format == erhe::dataformat::Format::format_undefined) ||
            (erhe::dataformat::get_stencil_size_bits(m_depth_stencil_format) == 0)
        ) {
            GTEST_SKIP() << "no depth+stencil attachment format available on this device";
        }

        m_debug_renderer = std::make_unique<Debug_renderer>(device());
    }

    void TearDown() override
    {
        m_debug_renderer.reset();
        Gpu_test::TearDown();
    }

    // Draws the case's line and returns the number of lit pixels on the
    // middle row of the target.
    [[nodiscard]] auto measure_line_width(const Line_case& line_case) -> int
    {
        erhe::graphics::Device& graphics_device = device();

        const int   width  = line_case.target_width;
        const int   height = c_target_height;
        const float aspect = static_cast<float>(width) / static_cast<float>(height);

        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);
        const std::shared_ptr<erhe::graphics::Texture> depth_stencil_target = std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = graphics_device,
                .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = m_depth_stencil_format,
                .width       = width,
                .height      = height,
                .debug_label = erhe::utility::Debug_label{"line width depth+stencil target"}
            }
        );

        // Camera at the origin looking down -z; the line is at z = -5, so its
        // depth is strictly inside (0, 1). The depth clear value follows the
        // convention Debug_renderer_bucket picks its depth compare from, so
        // the line passes the depth test whichever way depth runs.
        const bool reverse_depth = (graphics_device.get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
        const float near_z = 0.1f;
        const float far_z  = 100.0f;
        glm::mat4 clip_from_world{1.0f};
        glm::vec4 fov_sides{-1.0f, 1.0f, 1.0f, -1.0f};
        float     line_half_height = 0.5f;
        if (line_case.projection == Projection_kind::perspective) {
            clip_from_world = glm::perspectiveRH_ZO(line_case.fov_y, aspect, near_z, far_z);
            const float half_fov_y = 0.5f * line_case.fov_y;
            const float half_fov_x = std::atan(std::tan(half_fov_y) * aspect);
            fov_sides = glm::vec4{-half_fov_x, half_fov_x, half_fov_y, -half_fov_y};
            line_half_height = 5.0f * std::tan(half_fov_y) * 0.5f;
        } else {
            clip_from_world = glm::orthoRH_ZO(-aspect, aspect, -1.0f, 1.0f, near_z, far_z);
            fov_sides = glm::vec4{-aspect, aspect, 1.0f, -1.0f};
        }

        const erhe::math::Viewport viewport{0, 0, width, height};
        const View view{
            .clip_from_world        = clip_from_world,
            .viewport               = glm::vec4{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)},
            .fov_sides              = fov_sides,
            .view_position_in_world = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f},
            .pixel_scale            = line_case.pixel_scale
        };

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = color_target.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.depth_attachment.texture           = depth_stencil_target.get();
        descriptor.depth_attachment.clear_value[0]    = reverse_depth ? 0.0 : 1.0;
        descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
        descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Dont_care;
        descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::undefined;
        descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
        descriptor.stencil_attachment.texture         = depth_stencil_target.get();
        descriptor.stencil_attachment.clear_value[0]  = 0.0;
        descriptor.stencil_attachment.load_action     = erhe::graphics::Load_action::Clear;
        descriptor.stencil_attachment.store_action    = erhe::graphics::Store_action::Dont_care;
        descriptor.stencil_attachment.usage_before    = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        descriptor.stencil_attachment.layout_before   = erhe::graphics::Image_layout::undefined;
        descriptor.stencil_attachment.usage_after     = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        descriptor.stencil_attachment.layout_after    = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
        descriptor.render_target_width  = width;
        descriptor.render_target_height = height;
        descriptor.debug_label = erhe::utility::Debug_label{"line width"};

        Debug_renderer& debug_renderer = *m_debug_renderer;
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                debug_renderer.begin_frame(viewport, std::span<const View>{&view, 1});
                {
                    // Stencil reference 1 passes the debug pipeline's
                    // "greater" stencil test against the cleared 0.
                    Primitive_renderer line_renderer = debug_renderer.get(
                        Debug_renderer_config{
                            .primitive_type    = erhe::graphics::Primitive_type::line,
                            .stencil_reference = 1,
                            .draw_visible      = true,
                            .draw_hidden       = false
                        }
                    );
                    line_renderer.set_line_color(glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
                    line_renderer.set_thickness(line_case.thickness);
                    line_renderer.add_lines(
                        {
                            Line{
                                .p0 = glm::vec3{0.0f, -line_half_height, -5.0f},
                                .p1 = glm::vec3{0.0f,  line_half_height, -5.0f}
                            }
                        }
                    );
                }
                {
                    erhe::graphics::Compute_command_encoder compute_encoder = graphics_device.make_compute_command_encoder(command_buffer);
                    debug_renderer.compute(compute_encoder);
                }
                command_buffer.memory_barrier(
                    erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit |
                    erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit
                );
                {
                    erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                    erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                    const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                    encoder.set_scissor_rect(0, 0, width, height);
                    debug_renderer.render(encoder, render_pass, viewport);
                }
                debug_renderer.end_frame();
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
        EXPECT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        if (pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
            return -1;
        }
        const int row = height / 2;
        int lit = 0;
        for (int x = 0; x < width; ++x) {
            const std::size_t index = ((static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4u;
            if (pixels[index] > 127u) {
                ++lit;
            }
        }
        return lit;
    }

    std::unique_ptr<Debug_renderer> m_debug_renderer;
    erhe::dataformat::Format        m_depth_stencil_format{erhe::dataformat::Format::format_undefined};
};

// set_thickness(-4) is 4 pixels wide at every viewport width.
TEST_F(Debug_line_width_gpu_test, screen_space_width_independent_of_viewport_size)
{
    for (const int target_width : {128, 512, 1024}) {
        const int lit = measure_line_width(
            Line_case{
                .target_width = target_width,
                .projection   = Projection_kind::perspective,
                .fov_y        = 1.0f,
                .pixel_scale  = 1.0f,
                .thickness    = -4.0f
            }
        );
        EXPECT_EQ(lit, 4) << "viewport width " << target_width;
    }
}

// The width does not depend on the camera field of view.
TEST_F(Debug_line_width_gpu_test, screen_space_width_independent_of_fov)
{
    for (const float fov_y : {0.3f, 1.0f, 2.0f}) {
        const int lit = measure_line_width(
            Line_case{
                .target_width = 512,
                .projection   = Projection_kind::perspective,
                .fov_y        = fov_y,
                .pixel_scale  = 1.0f,
                .thickness    = -4.0f
            }
        );
        EXPECT_EQ(lit, 4) << "fov_y " << fov_y;
    }
}

// Orthographic projections give the same width at every viewport width.
TEST_F(Debug_line_width_gpu_test, screen_space_width_orthographic)
{
    for (const int target_width : {128, 1024}) {
        const int lit = measure_line_width(
            Line_case{
                .target_width = target_width,
                .projection   = Projection_kind::orthographic,
                .fov_y        = 0.0f,
                .pixel_scale  = 1.0f,
                .thickness    = -4.0f
            }
        );
        EXPECT_EQ(lit, 4) << "viewport width " << target_width;
    }
}

// View::pixel_scale converts logical pixels to framebuffer pixels.
TEST_F(Debug_line_width_gpu_test, screen_space_width_scales_with_pixel_scale)
{
    for (const int target_width : {128, 1024}) {
        const int lit_2x = measure_line_width(
            Line_case{
                .target_width = target_width,
                .projection   = Projection_kind::perspective,
                .fov_y        = 1.0f,
                .pixel_scale  = 2.0f,
                .thickness    = -4.0f
            }
        );
        EXPECT_EQ(lit_2x, 8) << "pixel scale 2, viewport width " << target_width;
        const int lit_1_5x = measure_line_width(
            Line_case{
                .target_width = target_width,
                .projection   = Projection_kind::perspective,
                .fov_y        = 1.0f,
                .pixel_scale  = 1.5f,
                .thickness    = -4.0f
            }
        );
        EXPECT_EQ(lit_1_5x, 6) << "pixel scale 1.5, viewport width " << target_width;
    }
}

} // namespace erhe::renderer::test
