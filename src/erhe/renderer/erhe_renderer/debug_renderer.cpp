#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"
#include "erhe_renderer/primitive_renderer.hpp"

#include "erhe_renderer/renderer_log.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::renderer {

Debug_renderer_program_interface::Debug_renderer_program_interface(erhe::graphics::Device& graphics_device)
    : fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , triangle_vertex_format{
        {
            0, // TODO
            {
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::position},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::custom, 0}, // clipped line start (xy) and end (zw)
            }
        }
    }
{
    // Line vertex (SSBO) buffer is input for line renderer compute shader.
    // It contains vertex positions (xyz), width (w) and colors (rgba).
    // These are written by CPU and read by compute shader (computer_before_line.comp in editor/res/shaders).
    line_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        "line_vertex_buffer",
        0,
        erhe::graphics::Shader_resource::Type::shader_storage_block
    );
    line_vertex_buffer_block->set_readonly(true);
    line_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "line_vertex");
    line_vertex_struct->add_vec4("position_0");
    line_vertex_struct->add_vec4("color_0");
    line_vertex_buffer_block->add_struct("vertices", line_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

    // Triangle vertex buffer will contain triangle vertex positions.
    // These are written by compute shader (when using line renderer),
    // after which they are read by vertex shader.
    triangle_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "triangle_vertex");
    triangle_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        "triangle_vertex_buffer",
        1,
        erhe::graphics::Shader_resource::Type::shader_storage_block
    );
    triangle_vertex_buffer_block->set_writeonly(true);
    add_vertex_stream(
        triangle_vertex_format.streams.front(),
        *triangle_vertex_struct.get(),
        *triangle_vertex_buffer_block.get()
    );

    view_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        "view",
        3,
        erhe::graphics::Shader_resource::Type::uniform_block
    );

    clip_from_world_offset = view_block->add_mat4("clip_from_world")->get_offset_in_parent();
    viewport_offset        = view_block->add_vec4("viewport"       )->get_offset_in_parent();
    fov_offset             = view_block->add_vec4("fov"            )->get_offset_in_parent();

    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");

    ERHE_VERIFY(graphics_device.get_info().use_compute_shader);
    {
        using namespace erhe::graphics;
        // Compute shader
        {
            const std::filesystem::path comp_path = shader_path / std::filesystem::path("compute_before_line.comp");
            Shader_stages_create_info create_info{
                .name             = "compute_before_line",
                .struct_types     = { line_vertex_struct.get(), triangle_vertex_struct.get() },
                .interface_blocks = { line_vertex_buffer_block.get(), triangle_vertex_buffer_block.get(), view_block.get() },
                .shaders          = { { Shader_type::compute_shader, comp_path }, }
            };

            Shader_stages_prototype prototype{graphics_device, create_info};
            if (prototype.is_valid()) {
                compute_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
                graphics_device.get_shader_monitor().add(create_info, compute_shader_stages.get());
            } else {
                const auto current_path = std::filesystem::current_path();
                log_startup->error(
                    "Unable to load Debug_renderer shader - check working directory '{}'",
                    current_path.string()
                );
            }
        }
        // Fragment shader
        {
            using namespace erhe::graphics;

            const std::filesystem::path vert_path = shader_path / std::filesystem::path("line_after_compute.vert");
            const std::filesystem::path frag_path = shader_path / std::filesystem::path("line_after_compute.frag");
            Shader_stages_create_info create_info{
                .name             = "line_after_compute",
                .interface_blocks = { view_block.get() },
                .fragment_outputs = &fragment_outputs,
                .vertex_format    = &triangle_vertex_format,
                .shaders = {
                    { Shader_type::vertex_shader,   vert_path },
                    { Shader_type::fragment_shader, frag_path }
                }
            };

            Shader_stages_prototype prototype{graphics_device, create_info};
            if (prototype.is_valid()) {
                graphics_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
                graphics_device.get_shader_monitor().add(create_info, graphics_shader_stages.get());
            } else {
                const auto current_path = std::filesystem::current_path();
                log_startup->error("Unable to load Debug_renderer shader - check working directory '{}'", current_path.string());
            }
        }
    }
}

Debug_renderer::Debug_renderer(erhe::graphics::Device& graphics_device)
    : m_graphics_device  {graphics_device}
    , m_program_interface{graphics_device}
    , m_vertex_input{
        graphics_device,
        erhe::graphics::Vertex_input_state_data::make(m_program_interface.triangle_vertex_format)
    }
    , m_lines_to_triangles_compute_pipeline{
        erhe::graphics::Compute_pipeline_data{
            .name          = "compute_before_line",
            .shader_stages = m_program_interface.compute_shader_stages.get()
        }
    }
{
}

Debug_renderer::~Debug_renderer() noexcept
{
}

auto Debug_renderer::get(const Debug_renderer_config& config) -> Primitive_renderer
{
    for (Debug_renderer_bucket& bucket : m_buckets) {
        if (bucket.match(config)) {
            bucket.start_view(get_view());
            return Primitive_renderer{*this, bucket};
        }
    }
    Debug_renderer_bucket& bucket = m_buckets.emplace_back(m_graphics_device, *this, config);
    bucket.start_view(get_view());
    return Primitive_renderer{*this, bucket};
}

static constexpr std::string_view c_line_renderer_render{"Debug_renderer::render()"};

void Debug_renderer::begin_frame(const erhe::math::Viewport viewport, const erhe::scene::Camera& camera)
{
    const auto* camera_node = camera.get_node();
    ERHE_VERIFY(camera_node != nullptr);

    const erhe::scene::Camera_projection_transforms projection_transforms = camera.projection_transforms(viewport);
    const glm::mat4                                 clip_from_world       = projection_transforms.clip_from_world.get_matrix();
    const erhe::scene::Projection::Fov_sides        fov_sides             = camera.projection()->get_fov_sides(viewport);

    for (size_t i = 0, end = m_view_stack.size(); i < end; ++i) {
        m_view_stack.pop();
    }

    push_view(
        {
            .clip_from_world = clip_from_world,
            .viewport        = glm::vec4{
                static_cast<float>(viewport.x),
                static_cast<float>(viewport.y),
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height)
            },
            .fov_sides = glm::vec4{
                fov_sides.left,
                fov_sides.right,
                fov_sides.up,
                fov_sides.down
            }
        }
    );
}

void Debug_renderer::compute(erhe::graphics::Compute_command_encoder& command_encoder)
{
    ERHE_VERIFY(m_graphics_device.get_info().use_compute_shader);

    command_encoder.set_compute_pipeline_state(m_lines_to_triangles_compute_pipeline);

    // Convert all lines to triangles using compute shader
    command_encoder.set_compute_pipeline_state(m_lines_to_triangles_compute_pipeline);
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.dispatch_compute(command_encoder);
    }
}

void Debug_renderer::render(erhe::graphics::Render_command_encoder& encoder, const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group scoped_debug_group{"Debug_renderer::render()"};

    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);

    // Draw hidden
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.render(encoder, true, false);
    }

    // Subsequent draw triangles reading vertex data must wait for compute shader
    m_graphics_device.memory_barrier(erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit);

    // Draw visible
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.render(encoder, false, true);
    }

    // Release buffers - now done in release()
    // for (Debug_renderer_bucket& bucket : m_buckets) {
    //     bucket.release_buffers();
    // }

    // Subsequent compute dispatch writing to triangle vertex buffer must wait for draw triangles reading that data
    // TODO Use gl::wait_sync()?
    m_graphics_device.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);

    ///// TODO investigate m_graphics_device.opengl_state_tracker.depth_stencil.reset(); // workaround issue in stencil state tracking
}

void Debug_renderer::end_frame()
{
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.release_buffers();
    }
}

} // namespace erhe::renderer
