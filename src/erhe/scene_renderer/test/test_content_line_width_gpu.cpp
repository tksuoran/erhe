// Content wide-line width (doc/erhe/scene_renderer.md "Content edge line widths").
//
// A negative Content_wide_line_renderer line width is a constant screen-space
// width: -N draws an edge N logical pixels wide, and
// Camera_view_input::pixel_scale converts logical pixels to framebuffer pixels.
// The width must not depend on the viewport size or the projection. Each case
// feeds one vertical edge through the renderer (compute expansion + graphics
// draw, the same shaders the editor uses) into an offscreen target and counts
// the lit pixels across the edge on the middle row.
//
// The edge sits at world x = 0, which the camera maps to NDC x = 0, the pixel
// boundary W/2 of an even target width W, so a ribbon of width N covers exactly
// N pixel centers: the count is exact.

#include "gpu_test_fixture.hpp"

#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/scene_log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace erhe::scene_renderer::test {

using erhe::graphics::test::Gpu_test;

namespace {

constexpr int c_target_height = 64;

enum class Projection_kind : unsigned int
{
    perspective,
    orthographic
};

class Edge_case
{
public:
    int             target_width;
    Projection_kind projection;
    float           fov_y;       // perspective only, radians
    float           pixel_scale;
    float           line_width;  // passed to Content_wide_line_renderer::add_mesh()
};

[[nodiscard]] auto shader_path() -> std::filesystem::path
{
    return std::filesystem::path{ERHE_SCENE_RENDERER_TEST_REPO_ROOT} / "res" / "shaders";
}

} // anonymous namespace

class Content_line_width_gpu_test : public Gpu_test
{
protected:
    void SetUp() override
    {
        Gpu_test::SetUp();

        static bool s_logging_initialized = false;
        if (!s_logging_initialized) {
            erhe::property::initialize_logging();
            erhe::item::initialize_logging();
            erhe::scene::initialize_logging();
            erhe::primitive::initialize_logging();
            erhe::scene_renderer::initialize_logging();
            s_logging_initialized = true;
        }

        erhe::graphics::Device& graphics_device = device();
        m_interface = std::make_unique<Content_wide_line_interface>(graphics_device, nullptr, 1);

        using namespace erhe::graphics;
        {
            Shader_stages_create_info create_info{
                .name             = "compute_before_content_line",
                .struct_types     = {
                    m_interface->edge_line_vertex_struct.get(),
                    m_interface->triangle_vertex_struct.get(),
                    &m_interface->view_camera_struct
                },
                .interface_blocks = {
                    m_interface->edge_line_vertex_buffer_block.get(),
                    m_interface->triangle_vertex_buffer_block.get(),
                    &m_interface->view_block
                },
                .shaders           = { { Shader_type::compute_shader, shader_path() / "compute_before_content_line.comp" } },
                .bind_group_layout = m_interface->bind_group_layout.get(),
            };
            Shader_stages_prototype prototype = build_shader_stages(graphics_device, create_info);
            ASSERT_TRUE(prototype.is_valid()) << "compute_before_content_line failed to compile";
            m_compute_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
        }
        {
            Shader_stages_create_info create_info{
                .name             = "content_line_after_compute",
                .struct_types     = {
                    m_interface->triangle_vertex_struct.get(),
                    &m_interface->view_camera_struct
                },
                .interface_blocks = {
                    m_interface->triangle_vertex_buffer_read_block.get(),
                    &m_interface->view_block
                },
                .fragment_outputs = &m_interface->fragment_outputs,
                .no_vertex_input  = true,
                .shaders = {
                    { Shader_type::vertex_shader,   shader_path() / "line_after_compute.vert"         },
                    { Shader_type::fragment_shader, shader_path() / "content_line_after_compute.frag" }
                },
                .bind_group_layout = m_interface->graphics_bind_group_layout.get(),
            };
            Shader_stages_prototype prototype = build_shader_stages(graphics_device, create_info);
            ASSERT_TRUE(prototype.is_valid()) << "content_line_after_compute failed to compile";
            m_graphics_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
        }

        m_renderer = make_content_wide_line_compute_renderer(
            graphics_device, *m_interface, m_compute_stages.get(), nullptr, m_graphics_stages.get(), nullptr
        );
        ASSERT_TRUE(m_renderer);

        m_mesh_memory = std::make_unique<Mesh_memory>(Mesh_memory_config{}, graphics_device);

        // One vertical edge from (0, -0.5, 0) to (0, 0.5, 0) facing +z (the
        // camera): struct edge_line_vertex { vec4 position; vec4 normal; }.
        const std::array<float, 16> edge_vertices{
            0.0f, -0.5f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f, 1.0f,
            0.0f,  0.5f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f, 1.0f
        };
        erhe::primitive::Buffer_mesh buffer_mesh{};
        erhe::primitive::Buffer_sink_allocation allocation = m_mesh_memory->allocate_vertex_buffer_range(
            m_mesh_memory->vertex_format_edge_line.streams.front(), 2
        );
        buffer_mesh.edge_line_vertex_buffer_range = allocation.range;
        buffer_mesh.edge_line_vertex_allocation   = std::move(allocation.allocation);
        std::vector<uint8_t> bytes(sizeof(edge_vertices));
        std::memcpy(bytes.data(), edge_vertices.data(), sizeof(edge_vertices));
        m_mesh_memory->enqueue_vertex_data(buffer_mesh.edge_line_vertex_buffer_range, std::move(bytes));

        m_mesh = std::make_shared<erhe::scene::Mesh>("edge");
        m_mesh->add_primitive(std::make_shared<erhe::primitive::Primitive>(std::move(buffer_mesh)));

        m_camera = std::make_shared<erhe::scene::Camera>("camera");
        m_camera->set_world_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 5.0f}));
    }

    void TearDown() override
    {
        m_mesh.reset();
        m_camera.reset();
        m_renderer.reset();
        m_mesh_memory.reset();
        m_graphics_stages.reset();
        m_compute_stages.reset();
        m_interface.reset();
        Gpu_test::TearDown();
    }

    // Draws the case's edge and returns the number of lit pixels on the
    // middle row of the target.
    [[nodiscard]] auto measure_edge_width(const Edge_case& edge_case) -> int
    {
        erhe::graphics::Device& graphics_device = device();
        const int width  = edge_case.target_width;
        const int height = c_target_height;

        erhe::scene::Projection projection{};
        if (edge_case.projection == Projection_kind::perspective) {
            projection.projection_type = erhe::scene::Projection::Type::perspective_vertical;
            projection.fov_y           = edge_case.fov_y;
        } else {
            projection.projection_type = erhe::scene::Projection::Type::orthographic_vertical;
            projection.ortho_height    = 2.0f;
        }
        m_camera->set_projection(projection);

        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

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
        descriptor.debug_label = erhe::utility::Debug_label{"content line width"};

        // Depth test off: the edge only needs to rasterize.
        erhe::graphics::Base_render_pipeline pipeline{
            graphics_device,
            erhe::graphics::Base_render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"content line width"},
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled
            }
        };
        erhe::graphics::Color_blend_state color_blend = erhe::graphics::Color_blend_state::color_blend_premultiplied;

        const erhe::math::Viewport viewport{0, 0, width, height};
        const Camera_view_input view_input{
            .projection  = m_camera->projection(),
            .node        = m_camera.get(),
            .viewport    = viewport,
            .pixel_scale = edge_case.pixel_scale
        };
        const bool reverse_depth = graphics_device.get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one;

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                m_mesh_memory->flush(command_buffer);
                m_renderer->begin_frame();
                m_renderer->set_view_params(
                    std::span<const Camera_view_input>{&view_input, 1},
                    reverse_depth,
                    graphics_device.get_info().coordinate_conventions.native_depth_range,
                    graphics_device.get_info().coordinate_conventions
                );
                m_renderer->add_mesh(*m_mesh_memory, *m_mesh, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, edge_case.line_width, 0);
                {
                    erhe::graphics::Compute_command_encoder compute_encoder = graphics_device.make_compute_command_encoder(command_buffer);
                    m_renderer->compute(compute_encoder);
                }
                command_buffer.memory_barrier(
                    erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit |
                    erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit
                );
                {
                    erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                    erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                    const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                    encoder.set_viewport_rect(0, 0, width, height);
                    encoder.set_scissor_rect (0, 0, width, height);
                    m_renderer->render(encoder, pipeline, &color_blend, 0);
                }
                m_renderer->end_frame();
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
        const std::size_t expected_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        EXPECT_EQ(pixels.size(), expected_size);
        if (pixels.size() != expected_size) {
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

    std::unique_ptr<Content_wide_line_interface>   m_interface;
    std::unique_ptr<erhe::graphics::Shader_stages> m_compute_stages;
    std::unique_ptr<erhe::graphics::Shader_stages> m_graphics_stages;
    std::unique_ptr<Content_wide_line_renderer>    m_renderer;
    std::unique_ptr<Mesh_memory>                   m_mesh_memory;
    std::shared_ptr<erhe::scene::Mesh>             m_mesh;
    std::shared_ptr<erhe::scene::Camera>           m_camera;
};

// A width of -4 is 4 pixels wide at every viewport width.
TEST_F(Content_line_width_gpu_test, screen_space_width_independent_of_viewport_size)
{
    for (const int target_width : {128, 512, 1024}) {
        const int lit = measure_edge_width(
            Edge_case{
                .target_width = target_width,
                .projection   = Projection_kind::perspective,
                .fov_y        = 1.0f,
                .pixel_scale  = 1.0f,
                .line_width   = -4.0f
            }
        );
        EXPECT_EQ(lit, 4) << "viewport width " << target_width;
    }
}

// The width does not depend on the camera field of view.
TEST_F(Content_line_width_gpu_test, screen_space_width_independent_of_fov)
{
    for (const float fov_y : {0.3f, 1.0f, 2.0f}) {
        const int lit = measure_edge_width(
            Edge_case{
                .target_width = 512,
                .projection   = Projection_kind::perspective,
                .fov_y        = fov_y,
                .pixel_scale  = 1.0f,
                .line_width   = -4.0f
            }
        );
        EXPECT_EQ(lit, 4) << "fov_y " << fov_y;
    }
}

// Orthographic projections give the same width at every viewport width.
TEST_F(Content_line_width_gpu_test, screen_space_width_orthographic)
{
    for (const int target_width : {128, 1024}) {
        const int lit = measure_edge_width(
            Edge_case{
                .target_width = target_width,
                .projection   = Projection_kind::orthographic,
                .fov_y        = 0.0f,
                .pixel_scale  = 1.0f,
                .line_width   = -4.0f
            }
        );
        EXPECT_EQ(lit, 4) << "viewport width " << target_width;
    }
}

// Camera_view_input::pixel_scale converts logical pixels to framebuffer pixels.
TEST_F(Content_line_width_gpu_test, screen_space_width_scales_with_pixel_scale)
{
    for (const int target_width : {128, 1024}) {
        const int lit_2x = measure_edge_width(
            Edge_case{
                .target_width = target_width,
                .projection   = Projection_kind::perspective,
                .fov_y        = 1.0f,
                .pixel_scale  = 2.0f,
                .line_width   = -4.0f
            }
        );
        EXPECT_EQ(lit_2x, 8) << "pixel scale 2, viewport width " << target_width;
        const int lit_1_5x = measure_edge_width(
            Edge_case{
                .target_width = target_width,
                .projection   = Projection_kind::perspective,
                .fov_y        = 1.0f,
                .pixel_scale  = 1.5f,
                .line_width   = -4.0f
            }
        );
        EXPECT_EQ(lit_1_5x, 6) << "pixel scale 1.5, viewport width " << target_width;
    }
}

} // namespace erhe::scene_renderer::test
