// Debug_renderer wide-line width and anti-aliasing (doc/erhe/renderer.md
// "Line widths", doc/plans/debug_renderer_anti_aliasing.md).
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
//
// The anti-aliasing tests shift the line by a fraction of a pixel (x_offset)
// and read the coverage profile of the middle row instead of a count.

#include "gpu_test_fixture.hpp"

#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_renderer/view.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <algorithm>
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

enum class Occlusion : unsigned int
{
    none,   // depth cleared to far: the line passes the visible pass
    behind  // depth cleared to near: only the hidden pass draws the line
};

enum class Cross_line : unsigned int
{
    none,
    after,  // a horizontal line of the same width drawn after the vertical one
    before  // ... drawn before it
};

class Line_case
{
public:
    int             target_width;
    Projection_kind projection;
    float           fov_y;                   // perspective only, radians
    float           pixel_scale;
    float           thickness;               // passed to Primitive_renderer::set_thickness()
    float           x_offset_pixels{0.0f};   // shift of the line from the pixel boundary at NDC x = 0
    int             repeat         {1};      // the line is added this many times
    float           alpha          {1.0f};   // line color alpha (color is white)
    Occlusion       occlusion      {Occlusion::none};
    bool            xray           {false};
    Cross_line      cross          {Cross_line::none};
    Anti_aliasing   anti_aliasing  {Anti_aliasing::on};
    float           background     {0.0f};   // clear color gray level
};

// Row profile helpers: values are the red channel (0..255) of the middle row.
[[nodiscard]] auto count_lit(const std::vector<uint8_t>& row) -> int
{
    int lit = 0;
    for (const uint8_t value : row) {
        if (value > 127u) {
            ++lit;
        }
    }
    return lit;
}

[[nodiscard]] auto coverage_sum(const std::vector<uint8_t>& row) -> float
{
    float sum = 0.0f;
    for (const uint8_t value : row) {
        sum += static_cast<float>(value) / 255.0f;
    }
    return sum;
}

[[nodiscard]] auto count_partial(const std::vector<uint8_t>& row) -> int
{
    int partial = 0;
    for (const uint8_t value : row) {
        if ((value > 0u) && (value < 255u)) {
            ++partial;
        }
    }
    return partial;
}

[[nodiscard]] auto max_value(const std::vector<uint8_t>& row) -> int
{
    int result = 0;
    for (const uint8_t value : row) {
        result = std::max(result, static_cast<int>(value));
    }
    return result;
}

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
        return count_lit(render_row(line_case));
    }

    // Draws the case's line(s) and returns the red channel of the middle row
    // of the target (empty on failure).
    [[nodiscard]] auto render_row(const Line_case& line_case) -> std::vector<uint8_t>
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
        float     frame_width_in_world = 2.0f * aspect; // world extent of the target width at z = -5
        if (line_case.projection == Projection_kind::perspective) {
            clip_from_world = glm::perspectiveRH_ZO(line_case.fov_y, aspect, near_z, far_z);
            const float half_fov_y = 0.5f * line_case.fov_y;
            const float half_fov_x = std::atan(std::tan(half_fov_y) * aspect);
            fov_sides = glm::vec4{-half_fov_x, half_fov_x, half_fov_y, -half_fov_y};
            line_half_height = 5.0f * std::tan(half_fov_y) * 0.5f;
            frame_width_in_world = 2.0f * 5.0f * std::tan(half_fov_x);
        } else {
            clip_from_world = glm::orthoRH_ZO(-aspect, aspect, -1.0f, 1.0f, near_z, far_z);
            fov_sides = glm::vec4{-aspect, aspect, 1.0f, -1.0f};
        }
        const float pixel_in_world = frame_width_in_world / static_cast<float>(width);
        const float line_x         = line_case.x_offset_pixels * pixel_in_world;
        const bool  occluded       = (line_case.occlusion == Occlusion::behind);

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
        const double background = static_cast<double>(line_case.background);
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ background, background, background, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.depth_attachment.texture           = depth_stencil_target.get();
        // Far (the line is visible) or near (the line is behind everything and
        // only the hidden pass draws it).
        descriptor.depth_attachment.clear_value[0]    = (reverse_depth != occluded) ? 0.0 : 1.0;
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
        debug_renderer.set_anti_aliasing(line_case.anti_aliasing);
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                debug_renderer.begin_frame(viewport, std::span<const View>{&view, 1});
                {
                    // Stencil reference 1 passes the debug pipeline's
                    // stencil test against the cleared 0.
                    Primitive_renderer line_renderer = debug_renderer.get(
                        Debug_renderer_config{
                            .primitive_type    = erhe::graphics::Primitive_type::line,
                            .stencil_reference = 1,
                            .draw_visible      = true,
                            .draw_hidden       = occluded,
                            .xray              = line_case.xray
                        }
                    );
                    line_renderer.set_line_color(glm::vec4{1.0f, 1.0f, 1.0f, line_case.alpha});
                    line_renderer.set_thickness(line_case.thickness);
                    const float cross_half_width = 0.5f * frame_width_in_world;
                    const auto add_cross = [&]() {
                        line_renderer.add_lines(
                            {
                                Line{
                                    .p0 = glm::vec3{-cross_half_width, 0.0f, -5.0f},
                                    .p1 = glm::vec3{ cross_half_width, 0.0f, -5.0f}
                                }
                            }
                        );
                    };
                    if (line_case.cross == Cross_line::before) {
                        add_cross();
                    }
                    for (int i = 0; i < line_case.repeat; ++i) {
                        line_renderer.add_lines(
                            {
                                Line{
                                    .p0 = glm::vec3{line_x, -line_half_height, -5.0f},
                                    .p1 = glm::vec3{line_x,  line_half_height, -5.0f}
                                }
                            }
                        );
                    }
                    if (line_case.cross == Cross_line::after) {
                        add_cross();
                    }
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
            return {};
        }
        const int row = height / 2;
        std::vector<uint8_t> result(static_cast<std::size_t>(width));
        for (int x = 0; x < width; ++x) {
            const std::size_t index = ((static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4u;
            result[static_cast<std::size_t>(x)] = pixels[index];
        }
        return result;
    }

    // Cost benchmark (plan section "Cost gate"): draws line_count random
    // wide lines (widths 1, 2 and 4 pixels, visible + hidden pass) into a
    // 1920 x 1080 target and returns the median GPU time of the render pass
    // over frame_count frames, in nanoseconds. The compute expansion is the
    // same in both modes and is outside the measured pass.
    [[nodiscard]] auto measure_pass_ns(const Anti_aliasing anti_aliasing, const int line_count, const int frame_count) -> uint64_t
    {
        erhe::graphics::Device& graphics_device = device();
        constexpr int width  = 1920;
        constexpr int height = 1080;
        const float   aspect = static_cast<float>(width) / static_cast<float>(height);

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
                .debug_label = erhe::utility::Debug_label{"line cost depth+stencil target"}
            }
        );

        const bool  reverse_depth = (graphics_device.get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
        const float fov_y         = 1.0f;
        const float half_fov_y    = 0.5f * fov_y;
        const float half_fov_x    = std::atan(std::tan(half_fov_y) * aspect);
        const glm::mat4 clip_from_world = glm::perspectiveRH_ZO(fov_y, aspect, 0.1f, 100.0f);
        const View view{
            .clip_from_world        = clip_from_world,
            .viewport               = glm::vec4{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)},
            .fov_sides              = glm::vec4{-half_fov_x, half_fov_x, half_fov_y, -half_fov_y},
            .view_position_in_world = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f},
            .pixel_scale            = 1.0f
        };
        const erhe::math::Viewport viewport{0, 0, width, height};

        // Deterministic random endpoints covering the view at z = -5 (+-1).
        const float half_w = 5.0f * std::tan(half_fov_x);
        const float half_h = 5.0f * std::tan(half_fov_y);
        uint32_t state = 12345u;
        const auto next_unit = [&state]() -> float {
            state = (state * 1664525u) + 1013904223u;
            return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
        };
        std::vector<Line> lines;
        lines.reserve(static_cast<std::size_t>(line_count));
        for (int i = 0; i < line_count; ++i) {
            lines.push_back(
                Line{
                    .p0 = glm::vec3{(next_unit() * 2.0f - 1.0f) * half_w, (next_unit() * 2.0f - 1.0f) * half_h, -5.0f + (next_unit() * 2.0f - 1.0f)},
                    .p1 = glm::vec3{(next_unit() * 2.0f - 1.0f) * half_w, (next_unit() * 2.0f - 1.0f) * half_h, -5.0f + (next_unit() * 2.0f - 1.0f)}
                }
            );
        }

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
        descriptor.debug_label = erhe::utility::Debug_label{"line cost"};

        Debug_renderer& debug_renderer = *m_debug_renderer;
        debug_renderer.set_anti_aliasing(anti_aliasing);

        erhe::graphics::Render_pass render_pass{graphics_device, descriptor};
        erhe::graphics::Gpu_timer   timer{render_pass, "line cost"};
        std::vector<uint64_t> samples;
        for (int frame = 0; frame < frame_count; ++frame) {
            submit_and_wait(
                [&](erhe::graphics::Command_buffer& command_buffer) {
                    debug_renderer.begin_frame(viewport, std::span<const View>{&view, 1});
                    {
                        Primitive_renderer line_renderer = debug_renderer.get(
                            Debug_renderer_config{
                                .primitive_type    = erhe::graphics::Primitive_type::line,
                                .stencil_reference = 1,
                                .draw_visible      = true,
                                .draw_hidden       = true
                            }
                        );
                        line_renderer.set_line_color(glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
                        const float widths[3] = { -1.0f, -2.0f, -4.0f };
                        for (std::size_t w = 0; w < 3; ++w) {
                            line_renderer.set_thickness(widths[w]);
                            const std::size_t begin = (w * lines.size()) / 3;
                            const std::size_t end   = ((w + 1) * lines.size()) / 3;
                            line_renderer.add_lines(glm::mat4{1.0f}, std::span<Line>{lines}.subspan(begin, end - begin));
                        }
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
                        erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                        const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                        encoder.set_scissor_rect(0, 0, width, height);
                        debug_renderer.render(encoder, render_pass, viewport);
                    }
                    debug_renderer.end_frame();
                }
            );
            const uint64_t ns = timer.last_result();
            if ((frame > 0) && (ns > 0u)) { // first frame warms pipelines
                samples.push_back(ns);
            }
        }
        if (samples.empty()) {
            return 0u;
        }
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
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

// --- Anti-aliasing (doc/plans/debug_renderer_anti_aliasing.md section 7) ---

namespace {

constexpr int c_aa_width = 512;

[[nodiscard]] auto aa_case(float thickness, float x_offset_pixels) -> Line_case
{
    return Line_case{
        .target_width    = c_aa_width,
        .projection      = Projection_kind::perspective,
        .fov_y           = 1.0f,
        .pixel_scale     = 1.0f,
        .thickness       = thickness,
        .x_offset_pixels = x_offset_pixels
    };
}

} // anonymous namespace

// 1. A 4-pixel line shifted by half a pixel covers three pixels fully and the
// two pixels beside them by half.
TEST_F(Debug_line_width_gpu_test, aa_half_pixel_offset_profile)
{
    const std::vector<uint8_t> row = render_row(aa_case(-4.0f, 0.5f));
    ASSERT_EQ(row.size(), static_cast<std::size_t>(c_aa_width));
    const int c = c_aa_width / 2;
    EXPECT_EQ(row[c - 3], 0u);
    EXPECT_NEAR(row[c - 2], 128, 2);
    EXPECT_EQ(row[c - 1], 255u);
    EXPECT_EQ(row[c    ], 255u);
    EXPECT_EQ(row[c + 1], 255u);
    EXPECT_NEAR(row[c + 2], 128, 2);
    EXPECT_EQ(row[c + 3], 0u);
}

// 2. The coverage across the line sums to the requested width for fractional
// widths and sub-pixel offsets alike (energy conservation).
TEST_F(Debug_line_width_gpu_test, aa_coverage_sum_equals_width)
{
    for (const float width : {0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f, 4.5f}) {
        for (const float offset : {0.0f, 0.5f, 0.25f}) {
            const std::vector<uint8_t> row = render_row(aa_case(-width, offset));
            ASSERT_EQ(row.size(), static_cast<std::size_t>(c_aa_width));
            const float sum       = coverage_sum(row);
            const float tolerance = 0.02f * width + 0.5f * static_cast<float>(count_partial(row) + 1) / 255.0f;
            EXPECT_NEAR(sum, width, tolerance) << "width " << width << " offset " << offset;
        }
    }
}

// 3. A quarter-pixel line stays one pixel wide at a quarter of the intensity.
TEST_F(Debug_line_width_gpu_test, aa_sub_pixel_width_fades)
{
    const std::vector<uint8_t> row = render_row(aa_case(-0.25f, 0.5f));
    ASSERT_EQ(row.size(), static_cast<std::size_t>(c_aa_width));
    EXPECT_NEAR(max_value(row), 64, 2);
    int nonzero = 0;
    for (const uint8_t value : row) {
        if (value > 0u) {
            ++nonzero;
        }
    }
    EXPECT_GE(nonzero, 1);
    EXPECT_LE(nonzero, 2);
}

// 4. An opaque line added twice renders exactly as once: the core draw
// overwrites, and the fringe draw's greater stencil compare lets only the
// first fringe fragment claim a pixel (so overlapping round caps of a
// polyline joint do not blend twice either). Widths 4 and 1 (a one-pixel
// line is fringe only).
TEST_F(Debug_line_width_gpu_test, aa_opaque_line_twice_equals_once)
{
    for (const float width : {4.0f, 1.0f}) {
        Line_case once = aa_case(-width, 0.5f);
        Line_case twice = once;
        twice.repeat = 2;
        const std::vector<uint8_t> once_row  = render_row(once);
        const std::vector<uint8_t> twice_row = render_row(twice);
        ASSERT_EQ(once_row.size(), static_cast<std::size_t>(c_aa_width));
        ASSERT_EQ(twice_row.size(), static_cast<std::size_t>(c_aa_width));
        const int c = c_aa_width / 2;
        EXPECT_NEAR(once_row[c - 2], (width == 4.0f) ? 128 : 0, 2) << "width " << width;
        for (std::size_t x = 0; x < once_row.size(); ++x) {
            EXPECT_EQ(once_row[x], twice_row[x]) << "width " << width << " x " << x;
        }
    }
}

// 5. A crossing line of the same color: every pixel of the row lies inside
// the horizontal line's core, so the row is fully lit whichever line is
// drawn first (a fringe never blocks or darkens a core).
TEST_F(Debug_line_width_gpu_test, aa_crossing_core_is_never_blocked)
{
    for (const Cross_line cross : {Cross_line::after, Cross_line::before}) {
        Line_case line_case = aa_case(-4.0f, 0.5f);
        line_case.cross = cross;
        const std::vector<uint8_t> row = render_row(line_case);
        ASSERT_EQ(row.size(), static_cast<std::size_t>(c_aa_width));
        for (std::size_t x = 0; x < row.size(); ++x) {
            EXPECT_GE(row[x], 254u) << "cross " << static_cast<unsigned int>(cross) << " x " << x;
        }
    }
}

// 5b. Translucent lines of one bucket blend on top of each other (last
// fragment wins), so the same line at alpha 0.5 twice is brighter than once.
TEST_F(Debug_line_width_gpu_test, translucent_line_twice_is_brighter_than_once)
{
    Line_case once = aa_case(-4.0f, 0.0f);
    once.alpha = 0.5f;
    Line_case twice = once;
    twice.repeat = 2;
    const std::vector<uint8_t> once_row  = render_row(once);
    const std::vector<uint8_t> twice_row = render_row(twice);
    ASSERT_EQ(once_row.size(), static_cast<std::size_t>(c_aa_width));
    ASSERT_EQ(twice_row.size(), static_cast<std::size_t>(c_aa_width));
    const int c = c_aa_width / 2;
    EXPECT_NEAR(once_row[c], 128, 2);
    EXPECT_NEAR(twice_row[c], 191, 2);
}

// 6. Behind everything, the hidden pass draws the same coverage profile at
// strength 0.1 ("over" the background, so a fringe pixel darkens the
// background only by 0.1 * coverage); an xray bucket draws it at full
// strength. The gray background tells this apart from a constant-factor
// blend, which would darken every ribbon pixel by the full 10 %.
TEST_F(Debug_line_width_gpu_test, aa_hidden_pass_is_dimmed_and_anti_aliased)
{
    constexpr float background = 0.5f;
    Line_case visible = aa_case(-4.0f, 0.5f);
    visible.background = background;
    Line_case hidden = visible;
    hidden.occlusion = Occlusion::behind;
    Line_case xray = hidden;
    xray.xray = true;
    const std::vector<uint8_t> visible_row = render_row(visible);
    const std::vector<uint8_t> hidden_row  = render_row(hidden);
    const std::vector<uint8_t> xray_row    = render_row(xray);
    ASSERT_EQ(visible_row.size(), static_cast<std::size_t>(c_aa_width));
    ASSERT_EQ(hidden_row.size(),  static_cast<std::size_t>(c_aa_width));
    ASSERT_EQ(xray_row.size(),    static_cast<std::size_t>(c_aa_width));
    const float bg = 255.0f * background;
    for (std::size_t x = 0; x < visible_row.size(); ++x) {
        // Coverage recovered from the visible row: visible = c * 255 + (1 - c) * bg.
        const float c        = (static_cast<float>(visible_row[x]) - bg) / (255.0f - bg);
        const float expected = (0.1f * c * 255.0f) + ((1.0f - (0.1f * c)) * bg);
        EXPECT_NEAR(static_cast<float>(hidden_row[x]), expected, 2.0f) << "x " << x;
        EXPECT_NEAR(xray_row[x], visible_row[x], 1) << "x " << x;
    }
}

// 7. With anti-aliasing off the edge is binary: the half-pixel-offset line
// lights exactly four pixels and no partial pixel exists.
TEST_F(Debug_line_width_gpu_test, aa_off_is_binary)
{
    Line_case line_case = aa_case(-4.0f, 0.5f);
    line_case.anti_aliasing = Anti_aliasing::off;
    const std::vector<uint8_t> row = render_row(line_case);
    ASSERT_EQ(row.size(), static_cast<std::size_t>(c_aa_width));
    EXPECT_EQ(count_lit(row), 4);
    EXPECT_EQ(count_partial(row), 0);
}

// Cost gate (plan section "Cost gate"): median render-pass GPU time of 2000
// random wide lines at 1920 x 1080, anti-aliasing off and on, logged to
// logs/log.txt of the repository root. The numbers are machine-dependent and recorded in the local
// memory bank; the test only checks that both modes produced a measurement.
TEST_F(Debug_line_width_gpu_test, aa_cost_benchmark)
{
    constexpr int line_count  = 2000;
    constexpr int frame_count = 21;
    const uint64_t off_ns = measure_pass_ns(Anti_aliasing::off, line_count, frame_count);
    const uint64_t on_ns  = measure_pass_ns(Anti_aliasing::on,  line_count, frame_count);
    EXPECT_GT(off_ns, 0u);
    EXPECT_GT(on_ns,  0u);
    const double ratio = (off_ns > 0u) ? (static_cast<double>(on_ns) / static_cast<double>(off_ns)) : 0.0;
    // Own info-level logger, as the GPU test environment does for the depth
    // convention, so logs/log.txt states the numbers without the editor's
    // logging configuration.
    const std::shared_ptr<spdlog::logger> log_gpu_test = erhe::log::make_logger("erhe.renderer.gpu_test");
    log_gpu_test->set_level(spdlog::level::info);
    log_gpu_test->info(
        "Debug line anti-aliasing cost: {} lines at 1920x1080, visible + hidden pass, median of {} frames: off {:.3f} ms, on {:.3f} ms, ratio {:.3f}",
        line_count, frame_count - 1,
        static_cast<double>(off_ns) * 1e-6, static_cast<double>(on_ns) * 1e-6, ratio
    );
}

} // namespace erhe::renderer::test
