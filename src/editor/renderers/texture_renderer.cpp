#include "renderers/texture_renderer.hpp"
#include "graphics/gl_context_provider.hpp"

#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Texture_renderer::Texture_renderer()
    : Component    {c_name}
    , Base_renderer{std::string{c_name}}
{
}

Texture_renderer::~Texture_renderer() = default;

void Texture_renderer::connect()
{
    base_connect(this);

    require<Gl_context_provider>();

    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Texture_renderer::initialize_component()
{
    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    m_vertex_input = std::make_unique<Vertex_input_state>();
}

void Texture_renderer::render(
    const erhe::graphics::Shader_stages* shader_stages,
    erhe::graphics::Texture*             texture
)
{

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::depth_attachment, texture, 0, 0);

    Framebuffer framebuffer{create_info};
    framebuffer.set_debug_label(
        fmt::format(
            "Texture renderer {} to {}",
            shader_stages->name(),
            texture->debug_label()
        )
    );

    m_pipeline_state_tracker->execute(
        erhe::graphics::Pipeline{
            {
                .name           = "Texture Renderer",
                .shader_stages  = get<Programs>()->depth.get(),
                .vertex_input   = m_vertex_input.get(),
                .input_assembly = Input_assembly_state::triangles,
                .rasterization  = Rasterization_state::cull_mode_none,
                .depth_stencil  = Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = Color_blend_state::color_blend_disabled
            }
        }
    );
    gl::viewport(0, 0, texture->width(), texture->height());

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer.gl_name());
    gl::draw_arrays(
        gl::Primitive_type::triangle_fan,
        0,
        4
    );
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
}


} // namespace editor
