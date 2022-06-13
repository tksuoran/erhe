#include "erhe/application/renderers/quad_renderer.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"

#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace erhe::application
{

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Quad_renderer::Quad_renderer()
    : Component{c_label}
{
}

Quad_renderer::~Quad_renderer() noexcept
{
}

void Quad_renderer::connect()
{
    require<Gl_context_provider>();

    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Quad_renderer::initialize_component()
{
    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    m_vertex_input    = std::make_unique<Vertex_input_state>();
    m_parameter_block = std::make_unique<erhe::graphics::Shader_resource>(
        "parameter",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    );

    m_fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    m_nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::nearest,
        gl::Texture_mag_filter::nearest
    );

    const auto texture = m_parameter_block->add_uvec2("texture");
    m_u_texture_size   = texture->size_bytes();
    m_u_texture_offset = texture->offset_in_parent();

    const auto shader_path = fs::path("res") / fs::path("shaders");
    const fs::path vs_path = shader_path / fs::path("quad.vert");
    const fs::path fs_path = shader_path / fs::path("quad.frag");
    erhe::graphics::Shader_stages::Create_info create_info{
        .name             = "quad",
        .fragment_outputs = &m_fragment_outputs,
    };
    create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    create_info.add_interface_block(m_parameter_block.get());
    create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    erhe::graphics::Shader_stages::Prototype prototype{create_info};
    m_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(std::move(prototype));
}

void Quad_renderer::create_frame_resources()
{
    for (size_t slot = 0; slot < s_frame_resources_count; ++slot)
    {
        m_frame_resources.emplace_back(
            m_shader_stages.get(),
            slot
        );
    }
}


auto Quad_renderer::current_frame_resources() -> Quad_renderer::Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Quad_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_parameter_writer.reset();
}

void Quad_renderer::render(const erhe::graphics::Texture& texture)
{
    m_parameter_writer.begin(current_frame_resources().parameter_buffer.target());

    const auto                handle           = texture.get_handle();
    auto&                     parameter_buffer = current_frame_resources().parameter_buffer;
    const auto                vertex_gpu_data  = parameter_buffer.map();
    std::byte* const          start            = vertex_gpu_data.data()       + m_parameter_writer.write_offset;
    const size_t              byte_count       = vertex_gpu_data.size_bytes() - m_parameter_writer.write_offset;
    const size_t              word_count       = byte_count / sizeof(float);
    const gsl::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const uint32_t texture_handle[2] =
    {
        static_cast<uint32_t>((handle & 0xffffffffu)),
        static_cast<uint32_t>(handle >> 32u)
    };
    const gsl::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};
    erhe::graphics::write(gpu_uint32_data, m_u_texture_offset, texture_handle_cpu_data);
    m_parameter_writer.write_offset += m_u_texture_size;
    m_parameter_writer.end();

    m_pipeline_state_tracker->execute(
        erhe::graphics::Pipeline{
            erhe::graphics::Pipeline_data{
                .name           = "Quad Renderer",
                .shader_stages  = m_shader_stages.get(),
                .vertex_input   = m_vertex_input.get(),
                .input_assembly = Input_assembly_state::triangles,
                .rasterization  = Rasterization_state::cull_mode_none,
                .depth_stencil  = Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = Color_blend_state::color_blend_disabled
            }
        }
    );

    gl::bind_buffer_range(
        parameter_buffer.target(),
        static_cast<GLuint>    (m_parameter_block->binding_point()),
        static_cast<GLuint>    (parameter_buffer.gl_name()),
        static_cast<GLintptr>  (m_parameter_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_parameter_writer.range.byte_count)
    );

    gl::make_texture_handle_resident_arb(handle);
    gl::draw_arrays(
        gl::Primitive_type::triangle_fan,
        0,
        4
    );
    gl::make_texture_handle_non_resident_arb(handle);
}

} // namespace erhe::application
