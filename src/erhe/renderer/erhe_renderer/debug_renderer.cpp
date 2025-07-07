#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"
#include "erhe_renderer/primitive_renderer.hpp"

#include "erhe_renderer/renderer_log.hpp"

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
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
            .type     = gl::Fragment_shader_output_type::float_vec4,
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

    clip_from_world_offset        = view_block->add_mat4("clip_from_world"       )->get_offset_in_parent();
    view_position_in_world_offset = view_block->add_vec4("view_position_in_world")->get_offset_in_parent();
    viewport_offset               = view_block->add_vec4("viewport"              )->get_offset_in_parent();
    fov_offset                    = view_block->add_vec4("fov"                   )->get_offset_in_parent();

    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");

    ERHE_VERIFY(graphics_device.info.use_compute_shader);
    {
        // Compute shader
        {
            const std::filesystem::path comp_path = shader_path / std::filesystem::path("compute_before_line.comp");
            erhe::graphics::Shader_stages_create_info create_info{
                .name             = "compute_before_line",
                .struct_types     = { line_vertex_struct.get(), triangle_vertex_struct.get() },
                .interface_blocks = { line_vertex_buffer_block.get(), triangle_vertex_buffer_block.get(), view_block.get() },
                .shaders          = { { gl::Shader_type::compute_shader, comp_path }, }
            };

            erhe::graphics::Shader_stages_prototype prototype{graphics_device, create_info};
            if (prototype.is_valid()) {
                compute_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(graphics_device, std::move(prototype));
                graphics_device.shader_monitor.add(create_info, compute_shader_stages.get());
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
            const std::filesystem::path vert_path = shader_path / std::filesystem::path("line_after_compute.vert");
            const std::filesystem::path frag_path = shader_path / std::filesystem::path("line_after_compute.frag");
            erhe::graphics::Shader_stages_create_info create_info{
                .name             = "line_after_compute",
                .interface_blocks = { view_block.get() },
                .fragment_outputs = &fragment_outputs,
                .vertex_format    = &triangle_vertex_format,
                .shaders = {
                    { gl::Shader_type::vertex_shader,   vert_path },
                    { gl::Shader_type::fragment_shader, frag_path }
                }
            };

            erhe::graphics::Shader_stages_prototype prototype{graphics_device, create_info};
            if (prototype.is_valid()) {
                graphics_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(graphics_device, std::move(prototype));
                graphics_device.shader_monitor.add(create_info, graphics_shader_stages.get());
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
    , m_view_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Debug_renderer::m_view_buffer",
        m_program_interface.view_block->get_binding_point()
    }
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

Debug_renderer::~Debug_renderer()
{
}

auto Debug_renderer::get(const Debug_renderer_config& config) -> Primitive_renderer
{
    for (Debug_renderer_bucket& bucket : m_buckets) {
        if (bucket.match(config)) {
            return Primitive_renderer{*this, bucket};
        }
    }
    Debug_renderer_bucket& bucket = m_buckets.emplace_back(m_graphics_device, *this, config);
    return Primitive_renderer{*this, bucket};
}

auto Debug_renderer::get(unsigned int stencil, bool visible, bool hidden) -> Primitive_renderer
{
    return get(
        Debug_renderer_config{
            .stencil_reference = stencil,
            .draw_visible      = visible,
            .draw_hidden       = hidden
        }
    );
}

static constexpr std::string_view c_line_renderer_render{"Debug_renderer::render()"};

auto Debug_renderer::update_view_buffer(const erhe::math::Viewport viewport, const erhe::scene::Camera& camera) -> erhe::graphics::Buffer_range
{
    const auto* camera_node = camera.get_node();
    ERHE_VERIFY(camera_node != nullptr);

    const erhe::graphics::Shader_resource& view_block = *m_program_interface.view_block.get();
    erhe::graphics::Buffer_range view_buffer_range = m_view_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, view_block.get_size_bytes());
    const auto                   view_gpu_data     = view_buffer_range.get_span();
    size_t                       view_write_offset = 0;
    std::byte* const             start             = view_gpu_data.data();
    const std::size_t            byte_count        = view_gpu_data.size_bytes();
    const std::size_t            word_count        = byte_count / sizeof(float);
    const std::span<float>       gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t>    gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const auto      projection_transforms  = camera.projection_transforms(viewport);
    const glm::mat4 clip_from_world        = projection_transforms.clip_from_world.get_matrix();
    const glm::vec4 view_position_in_world = camera_node->position_in_world();
    const auto      fov_sides              = camera.projection()->get_fov_sides(viewport);
    const float viewport_floats[4] {
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    };
    const float fov_floats[4] {
        fov_sides.left,
        fov_sides.right,
        fov_sides.up,
        fov_sides.down
    };

    using erhe::graphics::write;
    using erhe::graphics::as_span;
    write(view_gpu_data, m_program_interface.clip_from_world_offset,        as_span(clip_from_world       ));
    write(view_gpu_data, m_program_interface.view_position_in_world_offset, as_span(view_position_in_world));
    write(view_gpu_data, m_program_interface.viewport_offset,               as_span(viewport_floats       ));
    write(view_gpu_data, m_program_interface.fov_offset,                    as_span(fov_floats            ));

    view_write_offset += m_program_interface.view_block->get_size_bytes();
    view_buffer_range.bytes_written(view_write_offset);
    view_buffer_range.close();

    return view_buffer_range;
}

void Debug_renderer::update(const erhe::math::Viewport viewport, const erhe::scene::Camera& camera)
{
    m_view_buffer_range = update_view_buffer(viewport, camera);
}

void Debug_renderer::compute(erhe::graphics::Compute_command_encoder& command_encoder)
{
    ERHE_VERIFY(m_graphics_device.info.use_compute_shader);

    m_graphics_device.opengl_state_tracker.shader_stages.execute(m_program_interface.compute_shader_stages.get());
    m_view_buffer.bind(command_encoder, m_view_buffer_range);

    // Convert all lines to triangles using compute shader
    command_encoder.set_compute_pipeline_state(m_lines_to_triangles_compute_pipeline);
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.dispatch_compute(command_encoder);
    }

    // m_view_buffer_range.release(); done in release()
}

void Debug_renderer::render(erhe::graphics::Render_command_encoder& encoder, const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group scoped_debug_group{"Debug_renderer::render()"};

    gl::enable  (gl::Enable_cap::sample_alpha_to_coverage);
    gl::enable  (gl::Enable_cap::sample_alpha_to_one);
    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    m_view_buffer.bind(encoder, m_view_buffer_range);

    // Draw hidden
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.render(encoder, true, false);
    }

    // Subsequent draw triangles reading vertex data must wait for compute shader
    gl::memory_barrier(gl::Memory_barrier_mask::vertex_attrib_array_barrier_bit);

    // Draw visible
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.render(encoder, false, true);
    }

    // Release buffers - now done in release()
    // m_view_buffer_range.release();
    // for (Debug_renderer_bucket& bucket : m_buckets) {
    //     bucket.release_buffers();
    // }

    // Subsequent compute dispatch writing to triangle vertex buffer must wait for draw triangles reading that data
    // TODO Use gl::wait_sync()?
    gl::memory_barrier(gl::Memory_barrier_mask::shader_storage_barrier_bit);

    gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    gl::disable(gl::Enable_cap::sample_alpha_to_one);
    m_graphics_device.opengl_state_tracker.depth_stencil.reset(); // workaround issue in stencil state tracking
}

void Debug_renderer::release()
{
    m_view_buffer_range.release();
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.release_buffers();
    }
}

} // namespace erhe::renderer
