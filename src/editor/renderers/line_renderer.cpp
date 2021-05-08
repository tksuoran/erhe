#include "renderers/line_renderer.hpp"
#include "log.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics_experimental/shader_monitor.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"

#include "erhe_tracy.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iomanip>
#include <cstdarg>

namespace editor
{

using namespace erhe::graphics;
using namespace erhe::scene;
using namespace erhe::ui;
using namespace gl;
using namespace std;

using glm::mat4;
using glm::vec3;
using glm::vec4;

Line_renderer::Line_renderer()
    : Component("Line_renderer")
{
}

Line_renderer::~Line_renderer()
{
}

void Line_renderer::connect()
{
    m_pipeline_state_tracker = get<OpenGL_state_tracker>();
    m_shader_monitor         = require<Shader_monitor>();
}

static constexpr const char* c_line_renderer_initialize_component = "Line_renderer::initialize_component()";
void Line_renderer::initialize_component()
{
    ZoneScoped;

    gl::push_debug_group(gl::Debug_source::debug_source_application,
                         0,
                         static_cast<GLsizei>(strlen(c_line_renderer_initialize_component)),
                         c_line_renderer_initialize_component);

    m_fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    m_attribute_mappings.add(gl::Attribute_type::float_vec4, "a_position", {erhe::graphics::Vertex_attribute::Usage_type::position,  0}, 0);
    m_attribute_mappings.add(gl::Attribute_type::float_vec4, "a_color",    {erhe::graphics::Vertex_attribute::Usage_type::color,     0}, 1);

    m_vertex_format.make_attribute({erhe::graphics::Vertex_attribute::Usage_type::position, 0},
                                    gl::Attribute_type::float_vec4,
                                    {gl::Vertex_attrib_type::float_, false, 4});
    m_vertex_format.make_attribute({erhe::graphics::Vertex_attribute::Usage_type::color, 0},
                                    gl::Attribute_type::float_vec4,
                                    {gl::Vertex_attrib_type::unsigned_byte, true, 4});

    m_clip_from_world_offset        = m_model_block.add_mat4("clip_from_world").offset_in_parent();
    m_view_position_in_world_offset = m_model_block.add_vec4("view_position_in_world").offset_in_parent();

    auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
    std::filesystem::path vs_path = shader_path / std::filesystem::path("line.vert");
    std::filesystem::path fs_path = shader_path / std::filesystem::path("line.frag");
    Shader_stages::Create_info create_info("line",
                                           &m_default_uniform_block,
                                           &m_attribute_mappings,
                                           &m_fragment_outputs);
    create_info.add_interface_block(&m_model_block);
    create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    Shader_stages::Prototype prototype(create_info);
    m_shader_stages = std::make_unique<Shader_stages>(std::move(prototype));

    if (m_shader_monitor)
    {
        m_shader_monitor->add(create_info, m_shader_stages.get());
    }

    create_frame_resources();

    gl::pop_debug_group();
}

void Line_renderer::create_frame_resources()
{
    ZoneScoped;

    size_t vertex_count = 65536;
    for (size_t i = 0; i < s_frame_resources_count; ++i)
    {
        m_frame_resources.emplace_back(vertex_count,
                                       m_shader_stages.get(),
                                       m_attribute_mappings,
                                       m_vertex_format);
    }
}

auto Line_renderer::current_frame_resources() -> Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Line_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_vertex_writer.reset();
    m_line_count = 0;
}

void Line_renderer::add_lines(const std::initializer_list<Line> lines, float thickness)
{
    auto vertex_gpu_data = current_frame_resources().vertex_buffer.map();

    m_vertex_writer.begin();

    std::byte*          start      = vertex_gpu_data.data() + m_vertex_writer.write_offset;
    size_t              byte_count = vertex_gpu_data.size_bytes();
    size_t              word_count = byte_count / sizeof(float);
    gsl::span<float>    gpu_float_data(reinterpret_cast<float*   >(start), word_count);
    gsl::span<uint32_t> gpu_uint_data (reinterpret_cast<uint32_t*>(start), word_count);

    size_t word_offset = 0;
    for (const Line& line : lines)
    {
        gpu_float_data[word_offset++] = line.p0.x;
        gpu_float_data[word_offset++] = line.p0.y;
        gpu_float_data[word_offset++] = line.p0.z;
        gpu_float_data[word_offset++] = thickness;
        gpu_uint_data [word_offset++] = m_line_color;
        gpu_float_data[word_offset++] = line.p1.x;
        gpu_float_data[word_offset++] = line.p1.y;
        gpu_float_data[word_offset++] = line.p1.z;
        gpu_float_data[word_offset++] = thickness;
        gpu_uint_data [word_offset++] = m_line_color;
    }

    m_vertex_writer.write_offset += lines.size() * 2 * m_vertex_format.stride();
    m_line_count += lines.size();
    m_vertex_writer.end();
}

static constexpr const char* c_line_renderer_render = "Line_renderer::render()";
void Line_renderer::render(erhe::scene::Viewport       viewport,
                           const erhe::scene::ICamera& camera)
{
    if (m_line_count == 0)
    {
        return;
    }

    ZoneScoped;
    TracyGpuZone(c_line_renderer_render)

    gl::push_debug_group(gl::Debug_source::debug_source_application,
                         0,
                         static_cast<GLsizei>(strlen(c_line_renderer_render)),
                         c_line_renderer_render);

    mat4  clip_from_world        = camera.clip_from_world();
    vec4  view_position_in_world = camera.node()->position_in_world();
    auto* model_buffer           = &current_frame_resources().model_buffer;
    auto  model_gpu_data         = model_buffer->map();
    write(model_gpu_data, m_clip_from_world_offset,        as_span(clip_from_world));
    write(model_gpu_data, m_view_position_in_world_offset, as_span(view_position_in_world));

    gl::disable          (gl::Enable_cap::framebuffer_srgb);
    gl::disable          (gl::Enable_cap::primitive_restart_fixed_index);
    gl::viewport         (viewport.x, viewport.y, viewport.width, viewport.height);
    gl::bind_buffer_range(model_buffer->target(),
                          static_cast<GLuint>    (m_model_block.binding_point()),
                          static_cast<GLuint>    (model_buffer->gl_name()),
                          static_cast<GLintptr>  (0),
                          static_cast<GLsizeiptr>(4 * 4 * sizeof(float)));

    if (false)
    {
        const auto& pipeline = current_frame_resources().pipeline_depth_fail;
        m_pipeline_state_tracker->execute(&pipeline);

        gl::draw_arrays(pipeline.input_assembly->primitive_topology,
                        0,
                        static_cast<GLsizei>(m_line_count * 2));
    }

    if (true)
    {
        const auto& pipeline = current_frame_resources().pipeline_depth_pass;
        m_pipeline_state_tracker->execute(&pipeline);

        gl::draw_arrays(pipeline.input_assembly->primitive_topology,
                        0,
                        static_cast<GLsizei>(m_line_count * 2));
    }

    gl::pop_debug_group();
}


}
