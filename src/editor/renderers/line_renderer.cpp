#include "renderers/line_renderer.hpp"
#include "configuration.hpp"
#include "graphics/gl_context_provider.hpp"
#include "graphics/shader_monitor.hpp"
#include "windows/log_window.hpp"
#include "log.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

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
    : Component{c_name}
    , visible  {"visible"}
    , hidden   {"hidden"}
{
}

Line_renderer::~Line_renderer() = default;

void Line_renderer::connect()
{
    require<Gl_context_provider>();
    require<Configuration>();
    require<Shader_monitor>();

    m_pipeline_state_tracker = get<OpenGL_state_tracker>();
}

static constexpr std::string_view c_line_renderer_initialize_component{"Line_renderer::initialize_component()"};

void Line_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_line_renderer_initialize_component.length()),
        c_line_renderer_initialize_component.data()
    );

    m_pipeline.initialize(*get<Shader_monitor>().get());

    const Configuration& configuration = *get<Configuration>().get();
    visible.create_frame_resources(&m_pipeline, configuration);
    hidden.create_frame_resources(&m_pipeline, configuration);

    gl::pop_debug_group();
}

void Line_renderer::Pipeline::initialize(Shader_monitor& shader_monitor)
{
    fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    attribute_mappings.add(
        gl::Attribute_type::float_vec4,
        "a_position",
        {
            erhe::graphics::Vertex_attribute::Usage_type::position,
            0
        },
        0
    );
    attribute_mappings.add(
        gl::Attribute_type::float_vec4,
        "a_color",
        {
            erhe::graphics::Vertex_attribute::Usage_type::color,
            0
        },
        1
    );

    vertex_format.make_attribute(
        {
            erhe::graphics::Vertex_attribute::Usage_type::position,
            0
        },
        gl::Attribute_type::float_vec4,
        {
            gl::Vertex_attrib_type::float_,
            false,
            4
        }
    );
    vertex_format.make_attribute(
        {
            erhe::graphics::Vertex_attribute::Usage_type::color,
            0
        },
        gl::Attribute_type::float_vec4,
        {
            gl::Vertex_attrib_type::unsigned_byte,
            true,
            4
        }
    );

    view_block = std::make_unique<erhe::graphics::Shader_resource>(
        "view",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    );

    clip_from_world_offset        = view_block->add_mat4("clip_from_world"       )->offset_in_parent();
    view_position_in_world_offset = view_block->add_vec4("view_position_in_world")->offset_in_parent();
    viewport_offset               = view_block->add_vec4("viewport"              )->offset_in_parent();
    fov_offset                    = view_block->add_vec4("fov"                   )->offset_in_parent();

    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
    const std::filesystem::path vs_path = shader_path / std::filesystem::path("line.vert");
    const std::filesystem::path gs_path = shader_path / std::filesystem::path("line.geom");
    const std::filesystem::path fs_path = shader_path / std::filesystem::path("line.frag");
    Shader_stages::Create_info create_info{
        .name                      = "line",
        .vertex_attribute_mappings = &attribute_mappings,
        .fragment_outputs          = &fragment_outputs,
        .default_uniform_block     = &default_uniform_block
    };
    create_info.defines.push_back({"ERHE_LINE_SHADER_SHOW_DEBUG_LINES",        "0"});
    create_info.defines.push_back({"ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES", "1"});
    create_info.defines.push_back({"ERHE_LINE_SHADER_STRIP",                   "1"});
    create_info.add_interface_block(view_block.get());
    create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    create_info.shaders.emplace_back(gl::Shader_type::geometry_shader, gs_path);
    create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);

    Shader_stages::Prototype prototype(create_info);
    shader_stages = std::make_unique<Shader_stages>(std::move(prototype));

    shader_monitor.add(create_info, shader_stages.get());
}

Line_renderer::Style::Style(const char* name, bool world_space)
    : m_name       {name}
    , m_world_space{world_space}
{
}

void Line_renderer::Style::create_frame_resources(
    Pipeline*            pipeline,
    const Configuration& configuration
)
{
    ERHE_PROFILE_FUNCTION

    m_pipeline = pipeline;
    const auto       reverse_depth = configuration.reverse_depth;
    constexpr size_t vertex_count  = 65536;
    constexpr size_t view_stride   = 256;
    constexpr size_t view_count    = 16;
    for (size_t slot = 0; slot < s_frame_resources_count; ++slot)
    {
        m_frame_resources.emplace_back(
            reverse_depth,
            view_stride,
            view_count,
            vertex_count,
            pipeline->shader_stages.get(),
            pipeline->attribute_mappings,
            pipeline->vertex_format,
            m_name,
            slot
        );
    }
}

auto Line_renderer::Style::current_frame_resources() -> Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Line_renderer::next_frame()
{
    visible.next_frame();
    hidden.next_frame();
}

void Line_renderer::Style::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_view_writer  .reset();
    m_vertex_writer.reset();
    m_line_count = 0;
}

void Line_renderer::Style::put(
    const glm::vec3            point,
    const float                thickness,
    const uint32_t             color,
    const gsl::span<float>&    gpu_float_data, 
    const gsl::span<uint32_t>& gpu_uint_data,
    size_t&                    word_offset
)
{
    gpu_float_data[word_offset++] = point.x;
    gpu_float_data[word_offset++] = point.y;
    gpu_float_data[word_offset++] = point.z;
    gpu_float_data[word_offset++] = thickness;
    gpu_uint_data [word_offset++] = color;
}

void Line_renderer::Style::add_lines(
    const glm::mat4                   transform,
    const std::initializer_list<Line> lines,
    const float                       thickness
)
{
    auto vertex_gpu_data = current_frame_resources().vertex_buffer.map();

    m_vertex_writer.begin();

    std::byte* const          start      = vertex_gpu_data.data() + m_vertex_writer.write_offset;
    const size_t              byte_count = vertex_gpu_data.size_bytes();
    const size_t              word_count = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    size_t word_offset = 0;
    for (const Line& line : lines)
    {
        const glm::vec4 p0{transform * glm::vec4{line.p0, 1.0f}};
        const glm::vec4 p1{transform * glm::vec4{line.p1, 1.0f}};
        put(vec3{p0} / p0.w, thickness, m_line_color, gpu_float_data, gpu_uint_data, word_offset);
        put(vec3{p1} / p1.w, thickness, m_line_color, gpu_float_data, gpu_uint_data, word_offset);
    }

    m_vertex_writer.write_offset += lines.size() * 2 * m_pipeline->vertex_format.stride();
    m_line_count += lines.size();
    m_vertex_writer.end();
}

void Line_renderer::Style::add_lines(
    const std::initializer_list<Line> lines,
    const float                       thickness
)
{
    auto vertex_gpu_data = current_frame_resources().vertex_buffer.map();

    m_vertex_writer.begin();

    std::byte* const          start      = vertex_gpu_data.data() + m_vertex_writer.write_offset;
    const size_t              byte_count = vertex_gpu_data.size_bytes();
    const size_t              word_count = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    size_t word_offset = 0;
    for (const Line& line : lines)
    {
        put(line.p0, thickness, m_line_color, gpu_float_data, gpu_uint_data, word_offset);
        put(line.p1, thickness, m_line_color, gpu_float_data, gpu_uint_data, word_offset);
    }

    m_vertex_writer.write_offset += lines.size() * 2 * m_pipeline->vertex_format.stride();
    m_line_count += lines.size();
    m_vertex_writer.end();
}

static constexpr std::string_view c_line_renderer_render{"Line_renderer::render()"};

void Line_renderer::render(
    const erhe::scene::Viewport viewport,
    const erhe::scene::ICamera& camera
)
{
    auto& state_tracker = *m_pipeline_state_tracker.get();
    visible.render(state_tracker, viewport, camera, true, false);
    hidden.render(state_tracker, viewport, camera, true, true);
}

void Line_renderer::Style::render(
    erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker,
    const erhe::scene::Viewport           viewport,
    const erhe::scene::ICamera&           camera,
    const bool                            show_visible_lines,
    const bool                            show_hidden_lines
)
{
    if (m_line_count == 0)
    {
        return;
    }

    ERHE_PROFILE_FUNCTION
    ERHE_PROFILE_GPU_SCOPE(c_line_renderer_render.data())

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_line_renderer_render.length()),
        c_line_renderer_render.data()
    );

    m_view_writer.begin();

    const auto  projection_transforms  = camera.projection_transforms(viewport);
    const mat4  clip_from_world        = projection_transforms.clip_from_world.matrix();
    const vec4  view_position_in_world = camera.position_in_world();
    const auto  fov_sides              = camera.projection()->get_fov_sides(viewport);
    auto* const view_buffer            = &current_frame_resources().view_buffer;
    const auto  view_gpu_data          = view_buffer->map();
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

    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->clip_from_world_offset,        as_span(clip_from_world       ));
    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->view_position_in_world_offset, as_span(view_position_in_world));
    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->viewport_offset,               as_span(viewport_floats       ));
    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->fov_offset,                    as_span(fov_floats            ));

    m_view_writer.write_offset += m_pipeline->view_block->size_bytes();
    m_view_writer.end();

    gl::disable          (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable           (gl::Enable_cap::sample_alpha_to_coverage);
    gl::enable           (gl::Enable_cap::sample_alpha_to_one);
    gl::viewport         (viewport.x, viewport.y, viewport.width, viewport.height);
    gl::bind_buffer_range(
        view_buffer->target(),
        static_cast<GLuint>    (m_pipeline->view_block->binding_point()),
        static_cast<GLuint>    (view_buffer->gl_name()),
        static_cast<GLintptr>  (m_view_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_view_writer.range.byte_count)
    );

    if (show_hidden_lines)
    {
        const auto& pipeline = current_frame_resources().pipeline_depth_fail;
        pipeline_state_tracker.execute(&pipeline);

        gl::draw_arrays(
            pipeline.input_assembly->primitive_topology,
            0,
            static_cast<GLsizei>(m_line_count * 2)
        );
    }

    if (show_visible_lines)
    {
        const auto& pipeline = current_frame_resources().pipeline_depth_pass;
        pipeline_state_tracker.execute(&pipeline);

        gl::draw_arrays(
            pipeline.input_assembly->primitive_topology,
            0,
            static_cast<GLsizei>(m_line_count * 2)
        );
    }

    gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    gl::disable(gl::Enable_cap::sample_alpha_to_one);
    gl::pop_debug_group();
}


}
