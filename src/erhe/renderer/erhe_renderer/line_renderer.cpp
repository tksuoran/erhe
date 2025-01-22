#include "erhe_renderer/line_renderer.hpp"
#include "erhe_renderer/line_renderer_bucket.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"

#include "erhe_renderer/renderer_log.hpp"

#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::renderer {

Line_renderer_program_interface::Line_renderer_program_interface(erhe::graphics::Instance& graphics_instance)
    : fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , attribute_mappings{
        graphics_instance,
        {
            erhe::graphics::Vertex_attribute_mapping::a_position0_float_vec4(),
            erhe::graphics::Vertex_attribute_mapping::a_color_float_vec4(),
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 2,
                .shader_type     = erhe::graphics::Glsl_type::float_vec4,
                .name            = "a_line_start_end",
                .src_usage       = { erhe::graphics::Vertex_attribute::Usage_type::custom }
            }
        }
    }
    , line_vertex_format{
        erhe::graphics::Vertex_attribute::position0_float4(),
        erhe::graphics::Vertex_attribute::color_float4()
    }
    , triangle_vertex_format{
        erhe::graphics::Vertex_attribute::position0_float4(), // gl_Position
        erhe::graphics::Vertex_attribute::color_float4(),     // color
        erhe::graphics::Vertex_attribute{                     // clipped line start (xy) and end (zw)
            .usage       = { erhe::graphics::Vertex_attribute::Usage_type::custom },
            .shader_type = erhe::graphics::Glsl_type::float_vec4,
            .data_type   = erhe::dataformat::Format::format_32_vec4_float
        }
    }
{
    // Line vertex buffer will contain vertex positions, width and colors.
    // These are written by CPU are read by compute shader.
    // Vertex colors are are also read by the vertex shader
    line_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_instance,
        "line_vertex_buffer",
        0,
        erhe::graphics::Shader_resource::Type::shader_storage_block
    );
    line_vertex_buffer_block->set_readonly(true);
    line_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_instance, "line_vertex");
    line_vertex_format.add_to(
        *line_vertex_struct.get(),
        *line_vertex_buffer_block.get()
    );

    // Triangle vertex buffer will contain triangle vertex positions.
    // These are written by compute shader, after which they are read by vertex shader.
    triangle_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_instance, "triangle_vertex");
    triangle_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_instance,
        "triangle_vertex_buffer",
        1,
        erhe::graphics::Shader_resource::Type::shader_storage_block
    );
    triangle_vertex_buffer_block->set_writeonly(true);
    triangle_vertex_format.add_to(
        *triangle_vertex_struct.get(),
        *triangle_vertex_buffer_block.get()
    );

    view_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_instance,
        "view",
        3,
        erhe::graphics::Shader_resource::Type::uniform_block
    );

    clip_from_world_offset        = view_block->add_mat4("clip_from_world"       )->offset_in_parent();
    view_position_in_world_offset = view_block->add_vec4("view_position_in_world")->offset_in_parent();
    viewport_offset               = view_block->add_vec4("viewport"              )->offset_in_parent();
    fov_offset                    = view_block->add_vec4("fov"                   )->offset_in_parent();

    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
    {
        const std::filesystem::path comp_path = shader_path / std::filesystem::path("compute_before_line.comp");
        erhe::graphics::Shader_stages_create_info create_info{
            .name             = "compute_before_line",
            .struct_types     = { line_vertex_struct.get(), triangle_vertex_struct.get() },
            .interface_blocks = { line_vertex_buffer_block.get(), triangle_vertex_buffer_block.get(), view_block.get() },
            .shaders          = { { gl::Shader_type::compute_shader, comp_path }, }
        };

        erhe::graphics::Shader_stages_prototype prototype{graphics_instance, create_info};
        if (prototype.is_valid()) {
            compute_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(std::move(prototype));
            graphics_instance.shader_monitor.add(create_info, compute_shader_stages.get());
        } else {
            const auto current_path = std::filesystem::current_path();
            log_startup->error(
                "Unable to load Line_renderer shader - check working directory '{}'",
                current_path.string()
            );
        }
    }
    {
        const std::filesystem::path vert_path = shader_path / std::filesystem::path("line_after_compute.vert");
        const std::filesystem::path frag_path = shader_path / std::filesystem::path("line_after_compute.frag");
        erhe::graphics::Shader_stages_create_info create_info{
            .name                      = "line_after_compute",
            .interface_blocks          = { view_block.get() },
            .vertex_attribute_mappings = &attribute_mappings,
            .fragment_outputs          = &fragment_outputs,
            .shaders = {
                { gl::Shader_type::vertex_shader,   vert_path },
                { gl::Shader_type::fragment_shader, frag_path }
            }
        };

        erhe::graphics::Shader_stages_prototype prototype{graphics_instance, create_info};
        if (prototype.is_valid()) {
            graphics_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(std::move(prototype));
            graphics_instance.shader_monitor.add(create_info, graphics_shader_stages.get());
        } else {
            const auto current_path = std::filesystem::current_path();
            log_startup->error("Unable to load Line_renderer shader - check working directory '{}'", current_path.string());
        }
    }
}

Line_renderer::Line_renderer(erhe::graphics::Instance& graphics_instance)
    : m_graphics_instance{graphics_instance}
    , m_program_interface{graphics_instance}
    , m_line_vertex_buffer{
        graphics_instance,
        gl::Buffer_target::shader_storage_buffer,
        m_program_interface.line_vertex_buffer_block->binding_point(),
        m_program_interface.line_vertex_format.stride() * 2 * s_max_line_count,
        "Line_renderer line vertex ring buffer"
    }
    , m_triangle_vertex_buffer{
        graphics_instance,
        gl::Buffer_target::array_buffer,
        m_program_interface.triangle_vertex_format.stride() * 6 * s_max_line_count,
        "Line_renderer triangle vertex ring buffer"
    }
    , m_view_buffer{
        graphics_instance,
        gl::Buffer_target::uniform_buffer,
        m_program_interface.view_block->binding_point(),
        s_view_stride * s_max_view_count,
        "Line_renderer view ring buffer"
    }
    , m_vertex_input{
        erhe::graphics::Vertex_input_state_data::make(
            m_program_interface.attribute_mappings,
            m_program_interface.triangle_vertex_format,
            &m_triangle_vertex_buffer.get_buffer(),
            nullptr
        )
    }
{
}

auto Line_renderer::get(const Line_renderer_config& config) -> Scoped_line_renderer
{
    for (Line_renderer_bucket& bucket : m_buckets) {
        if (bucket.match(config)) {
            return Scoped_line_renderer{*this, bucket, config.indirect};
        }
    }
    Line_renderer_bucket& bucket = m_buckets.emplace_back(*this, config);
    return Scoped_line_renderer{*this, bucket, config.indirect};
}

auto Line_renderer::get(unsigned int stencil, bool visible, bool hidden) -> Scoped_line_renderer
{
    return get(
        Line_renderer_config{
            .stencil_reference = stencil,
            .draw_visible      = visible,
            .draw_hidden       = hidden,
            .reverse_depth     = true,
            .indirect          = false
        }
    );
}

auto Line_renderer::get_indirect(unsigned int stencil, bool visible, bool hidden) -> Scoped_line_renderer
{
    return get(
        Line_renderer_config{
            .stencil_reference = stencil,
            .draw_visible      = visible,
            .draw_hidden       = hidden,
            .reverse_depth     = true,
            .indirect          = true
        }
    );
}

auto Line_renderer::get_line_offset() const -> std::size_t
{
    ERHE_VERIFY(m_vertex_buffer_range.has_value());
    const std::size_t bytes_per_line = 2 * m_program_interface.line_vertex_format.stride();
    return m_vertex_write_offset / bytes_per_line;
}

auto Line_renderer::allocate_vertex_subspan(std::size_t byte_count) -> std::span<std::byte>
{
    ERHE_VERIFY(m_vertex_buffer_range.has_value());
    return m_vertex_buffer_range.value().get_span().subspan(byte_count);
}

void Line_renderer::begin()
{
    ERHE_VERIFY(!m_inside_begin_end);

    if (!m_vertex_buffer_range.has_value()) {
        m_vertex_buffer_range = m_line_vertex_buffer.open_cpu_write(0);
        m_vertex_write_offset = 0;
    }

    for (Line_renderer_bucket& bucket : m_buckets) {
        bucket.begin_frame();
    }
    m_inside_begin_end = true;
}

auto Line_renderer::verify_inside_begin_end() const -> bool
{
    ERHE_VERIFY(m_inside_begin_end);
    ERHE_VERIFY(m_vertex_buffer_range.has_value());
    return m_inside_begin_end;
}

void Line_renderer::end()
{
    ERHE_VERIFY(m_inside_begin_end);
    ERHE_VERIFY(m_vertex_buffer_range.has_value());
    
    m_inside_begin_end = false;
    
    m_vertex_buffer_range->flush(m_vertex_write_offset);
}

static constexpr std::string_view c_line_renderer_render{"Line_renderer::render()"};

void Line_renderer::render(const erhe::math::Viewport viewport, const erhe::scene::Camera& camera)
{
    ERHE_PROFILE_FUNCTION();

    if (m_vertex_buffer_range.has_value()) {
        m_vertex_buffer_range->close(m_vertex_write_offset);
    }

    const auto* camera_node = camera.get_node();
    if (
        (camera_node == nullptr) ||
        !m_vertex_buffer_range.has_value() ||
        (m_vertex_write_offset == 0)
    )
    {
        if (m_vertex_buffer_range.has_value()) {
            m_vertex_buffer_range->cancel();
        }
        return;
    }

    Buffer_range& vertex_buffer_range = m_vertex_buffer_range.value();

    std::size_t line_vertex_stride     = m_program_interface.line_vertex_format.stride();
    std::size_t triangle_vertex_stride = m_program_interface.triangle_vertex_format.stride();
    std::size_t slot_line_vertex_count = vertex_buffer_range.get_written_byte_count() / line_vertex_stride;
    std::size_t slot_first_vertex      = vertex_buffer_range.get_byte_start_offset_in_buffer() / line_vertex_stride;
    std::size_t slot_first_line        = slot_first_vertex / 2;
    std::size_t slot_line_count        = slot_line_vertex_count / 2;

    ERHE_VERIFY(slot_line_count > 0);

    //ERHE_PROFILE_GPU_SCOPE(c_line_renderer_render)

    erhe::graphics::Scoped_debug_group scoped_debug_group{c_line_renderer_render};

    const erhe::graphics::Shader_resource& view_block                   = *m_program_interface.view_block.get();
    const erhe::graphics::Shader_resource& triangle_vertex_buffer_block = *m_program_interface.triangle_vertex_buffer_block.get();
    auto* const               triangle_vertex_buffer = &get_triangle_vertex_buffer();
    Buffer_range              view_buffer_range      = m_view_buffer.open_cpu_write(view_block.size_bytes());
    const auto                view_gpu_data          = view_buffer_range.get_span();
    size_t                    view_write_offset      = 0;
    std::byte* const          start                  = view_gpu_data.data();
    const std::size_t         byte_count             = view_gpu_data.size_bytes();
    const std::size_t         word_count             = byte_count / sizeof(float);
    const std::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

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

    view_write_offset += m_program_interface.view_block->size_bytes();
    view_buffer_range.close(view_write_offset);

    // Bind buffers for compute
    view_buffer_range.bind();
    //gl::bind_buffer_range(
    //    view_block.get_binding_target(),
    //    static_cast<GLuint>    (view_block.binding_point()),
    //    static_cast<GLuint>    (view_buffer->gl_name()),
    //    static_cast<GLintptr>  (m_view_writer.range.first_byte_offset),
    //    static_cast<GLsizeiptr>(m_view_writer.range.byte_count)
    //);

    // line vertex buffer: 2 vertices per line
    vertex_buffer_range.bind();
    //gl::bind_buffer_range(
    //    line_vertex_buffer_block.get_binding_target(),
    //    static_cast<GLuint>    (line_vertex_buffer_block.binding_point()),
    //    static_cast<GLuint>    (line_vertex_buffer->gl_name()),
    //    static_cast<GLintptr>  (m_vertex_writer.range.first_byte_offset),
    //    static_cast<GLsizeiptr>(m_vertex_writer.range.byte_count)
    //);

    // triangle vertex buffer: 6 vertices per line
    gl::bind_buffer_range(
        triangle_vertex_buffer_block.get_binding_target(),
        static_cast<GLuint>    (triangle_vertex_buffer_block.binding_point()),
        static_cast<GLuint>    (triangle_vertex_buffer->gl_name()),
        static_cast<GLintptr>  (6 * slot_first_line * triangle_vertex_stride),
        static_cast<GLsizeiptr>(6 * slot_line_count * triangle_vertex_stride)
    );

    // Convert all lines to triangles using compute shader
    m_graphics_instance.opengl_state_tracker.shader_stages.execute(m_program_interface.compute_shader_stages.get());
    gl::dispatch_compute(static_cast<unsigned int>(slot_line_count), 1, 1);

    // Subsequent draw triangles reading vertex data must wait for compute shader
    gl::memory_barrier(gl::Memory_barrier_mask::vertex_attrib_array_barrier_bit);

    // Note: Bind buffers for graphics is done by Line_renderer_bucket::render() 
    //       calling graphics_instance.opengl_state_tracker.execute();

    //// gl::disable         (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable          (gl::Enable_cap::sample_alpha_to_coverage);
    gl::enable          (gl::Enable_cap::sample_alpha_to_one);
    gl::viewport        (viewport.x, viewport.y, viewport.width, viewport.height);

    // Draw hidden
    for (Line_renderer_bucket& bucket : m_buckets) {
        bucket.render(m_graphics_instance, true, false);
    }

    // Draw visible
    for (Line_renderer_bucket& bucket : m_buckets) {
        bucket.render(m_graphics_instance, false, true);
    }

    vertex_buffer_range.submit(); // this maybe can be moved after gl::dispatch_compute() call
    view_buffer_range.submit();

    m_vertex_buffer_range.reset();
    m_vertex_write_offset = 0;

    // Subsequent compute dispatch writing to triangle vertex buffer must wait for draw triangles reading that data
    // Except, we rotate 4 section within the buffer, and hope there are at most 4 frames in flight.
    // gl::memory_barrier(gl::Memory_barrier_mask::shader_storage_barrier_bit);

    gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    gl::disable(gl::Enable_cap::sample_alpha_to_one);
    m_graphics_instance.opengl_state_tracker.depth_stencil.reset(); // workaround issue in stencil state tracking
}

} // namespace erhe::renderer
