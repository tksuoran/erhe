#include "renderers/shadow_renderer.hpp"
#include "configuration.hpp"
#include "graphics/gl_context_provider.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "scene/scene_manager.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace editor
{

using namespace erhe::toolkit;
using namespace erhe::graphics;
using namespace erhe::primitive;
using namespace erhe::scene;
using namespace gl;
using namespace glm;
using namespace std;

Shadow_renderer::Shadow_renderer()
    : Component(c_name)
{
}

Shadow_renderer::~Shadow_renderer() = default;

void Shadow_renderer::connect()
{
    base_connect(this);

    require<Gl_context_provider>();
    require<Program_interface>();
    require<Programs>();

    m_mesh_memory            = require<Mesh_memory>();
    m_configuration          = require<Configuration>();
    m_pipeline_state_tracker = get<OpenGL_state_tracker>();
}

static constexpr std::string_view c_shadow_renderer_initialize_component{"Shadow_renderer::initialize_component()"};

void Shadow_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_shadow_renderer_initialize_component.length()),
        c_shadow_renderer_initialize_component.data()
    );

    create_frame_resources(1, 256, 256, 1000, 1000);

    const auto& shader_resources = *get<Program_interface>()->shader_resources.get();
    m_vertex_input = std::make_unique<Vertex_input_state>(
        shader_resources.attribute_mappings,
        m_mesh_memory->gl_vertex_format(),
        m_mesh_memory->gl_vertex_buffer.get(),
        m_mesh_memory->gl_index_buffer.get()
    );

    m_pipeline.shader_stages  = get<Programs>()->depth.get();
    m_pipeline.vertex_input   = m_vertex_input.get();
    m_pipeline.input_assembly = &Input_assembly_state::triangles;
    m_pipeline.rasterization  = &Rasterization_state::cull_mode_none; //cull_mode_back_ccw;
    m_pipeline.depth_stencil  =  Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth);
    m_pipeline.color_blend    = &Color_blend_state::color_writes_disabled;
    m_pipeline.viewport       = nullptr;

    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

        Texture::Create_info create_info;
        create_info.target          = gl::Texture_target::texture_2d_array;
        create_info.internal_format = gl::Internal_format::depth_component32f;
        create_info.width           = s_enable ? s_texture_resolution : 1;
        create_info.height          = s_enable ? s_texture_resolution : 1;
        create_info.depth           = s_max_light_count;
        m_texture = std::make_unique<Texture>(create_info);
        m_texture->set_debug_label("Shadow texture");
        //float depth_clear_value = erhe::graphics::Instance::depth_clear_value;
        //gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
    }

    for (size_t i = 0; i < s_max_light_count; ++i)
    {
        ERHE_PROFILE_SCOPE("framebuffer creation");

        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::depth_attachment, m_texture.get(), 0, static_cast<unsigned int>(i));
        m_framebuffers.emplace_back(std::make_unique<Framebuffer>(create_info));
    }

    m_viewport.x             = 0;
    m_viewport.y             = 0;
    m_viewport.width         = m_texture->width();
    m_viewport.height        = m_texture->height();
    m_viewport.reverse_depth = m_configuration->reverse_depth;

    gl::pop_debug_group();
}

static constexpr std::string_view c_shadow_renderer_render{"Shadow_renderer::render()"};

void Shadow_renderer::render(
    const Mesh_layer_collection&    mesh_layers,
    const erhe::scene::Light_layer& light_layer
)
{
    if constexpr (!s_enable)
    {
        return;
    }

    ERHE_PROFILE_FUNCTION

    ERHE_PROFILE_GPU_SCOPE(c_shadow_renderer_render.data())

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_shadow_renderer_render.length()),
        c_shadow_renderer_render.data()
    );

    m_pipeline_state_tracker->execute(&m_pipeline);
    gl::viewport(m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);

    erhe::scene::Visibility_filter shadow_filter{
        erhe::scene::Node::c_visibility_shadow_cast,
        0u,
        0u,
        0u
    };

    update_light_buffer(light_layer, m_viewport);
    for (auto mesh_layer : mesh_layers)
    {
        update_primitive_buffer(*mesh_layer, shadow_filter);
        auto draw_indirect_buffer_range = update_draw_indirect_buffer(
            *mesh_layer,
            Primitive_mode::polygon_fill,
            shadow_filter
        );

        bind_light_buffer();
        bind_primitive_buffer();
        bind_draw_indirect_buffer();

        size_t light_index = 0;
        for (auto light : light_layer.lights)
        {
            if (light_index >= s_max_light_count)
            {
                break; // TODO
            }
            light->update(m_viewport);
            if (!light->cast_shadow)
            {
                continue;
            }

            update_camera_buffer(*light.get(), m_viewport);

            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffers[light_index]->gl_name());
            gl::clear_buffer_fv(gl::Buffer::depth, 0, m_configuration->depth_clear_value_pointer());

            bind_camera_buffer();

            gl::multi_draw_elements_indirect(
                m_pipeline.input_assembly->primitive_topology,
                m_mesh_memory->gl_index_type(),
                reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
            );
            ++light_index;
        }
    }

    gl::pop_debug_group();
}

auto Shadow_renderer::texture() const -> erhe::graphics::Texture*
{
    return m_texture.get();
}

auto Shadow_renderer::viewport() const -> erhe::scene::Viewport
{
    return m_viewport;
}

} // namespace editor
