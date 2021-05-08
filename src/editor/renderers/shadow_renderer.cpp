#include "renderers/shadow_renderer.hpp"
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
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"

#include "erhe_tracy.hpp"

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
    : Component("Shadow_renderer")
{
}

void Shadow_renderer::connect()
{
    base_connect(this);

    m_pipeline_state_tracker = require<OpenGL_state_tracker>();
    m_scene_manager          = require<Scene_manager>();
}

static constexpr const char* c_shadow_renderer_initialize_component = "Shadow_renderer::initialize_component()";

void Shadow_renderer::initialize_component()
{
    ZoneScoped;

    gl::push_debug_group(gl::Debug_source::debug_source_application,
                         0,
                         static_cast<GLsizei>(strlen(c_shadow_renderer_initialize_component)),
                         c_shadow_renderer_initialize_component);

    create_frame_resources(1, 256, 256, 1000, 1000);

    m_vertex_input = std::make_unique<Vertex_input_state>(programs()->attribute_mappings,
                                                          *m_scene_manager->vertex_format(),
                                                          m_scene_manager->vertex_buffer(),
                                                          m_scene_manager->index_buffer());

    m_pipeline.shader_stages  = programs()->depth.get();
    m_pipeline.vertex_input   = m_vertex_input.get();
    m_pipeline.input_assembly = &Input_assembly_state::triangles;
    m_pipeline.rasterization  = &Rasterization_state::cull_mode_none; //cull_mode_back_ccw;
    m_pipeline.depth_stencil  = &Depth_stencil_state::depth_test_enabled_stencil_test_disabled;
    m_pipeline.color_blend    = &Color_blend_state::color_writes_disabled;
    m_pipeline.viewport       = nullptr;

    {
        ZoneScopedN("allocating shadow map array texture");
        Texture::Create_info create_info;
        create_info.target          = gl::Texture_target::texture_2d_array;
        create_info.internal_format = gl::Internal_format::depth_component32f;
        create_info.width           = s_enable ? s_texture_resolution : 1;
        create_info.height          = s_enable ? s_texture_resolution : 1;
        create_info.depth           = s_max_light_count;
        m_texture = std::make_unique<Texture>(create_info);
        m_texture->set_debug_label("Shadow texture");
        //float depth_clear_value = erhe::graphics::Configuration::depth_clear_value;
        //gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
    }

    for (size_t i = 0; i < s_max_light_count; ++i)
    {
        ZoneScopedN("framebuffer creation");
        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::depth_attachment, m_texture.get(), 0, static_cast<unsigned int>(i));
        m_framebuffers.emplace_back(std::make_unique<Framebuffer>(create_info));
    }

    m_viewport.x      = 0;
    m_viewport.y      = 0;
    m_viewport.width  = m_texture->width();
    m_viewport.height = m_texture->height();

    gl::pop_debug_group();
}

static constexpr const char* c_shadow_renderer_render = "Shadow_renderer::render()";
void Shadow_renderer::render(Layer_collection& layers,
                             const ICamera&    camera)
{
    if (s_enable == false)
    {
        return;
    }

    ZoneScoped;
    TracyGpuZone(c_shadow_renderer_render)

    gl::push_debug_group(gl::Debug_source::debug_source_application,
                         0,
                         static_cast<GLsizei>(strlen(c_shadow_renderer_render)),
                         c_shadow_renderer_render);

    // For now, camera is ignored.
    static_cast<void>(camera);

    m_pipeline_state_tracker->execute(&m_pipeline);
    gl::viewport(m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);

    for (auto layer : layers)
    {
        update_light_buffer(layer->lights, m_viewport);
        update_primitive_buffer(layer->meshes, erhe::scene::Mesh::c_visibility_shadow_cast);
        auto draw_indirect_buffer_range = update_draw_indirect_buffer(layer->meshes,
                                                                      Primitive_geometry::Mode::polygon_fill,
                                                                      erhe::scene::Mesh::c_visibility_shadow_cast);

        bind_light_buffer();
        bind_primitive_buffer();
        bind_draw_indirect_buffer();

        size_t light_index = 0;
        for (auto light : layer->lights)
        {
            light->update(m_viewport);
            if (!light->cast_shadow)
            {
                continue;
            }

            update_camera_buffer(*light.get(), m_viewport);

            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffers[light_index]->gl_name());
            gl::clear_buffer_fv(gl::Buffer::depth, 0, &erhe::graphics::Configuration::depth_clear_value);

            bind_camera_buffer();

            gl::multi_draw_elements_indirect(m_pipeline.input_assembly->primitive_topology,
                                             m_scene_manager->index_type(),
                                             reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                                             static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                                             static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command)));
            ++light_index;
        }
    }

    gl::pop_debug_group();
}

} // namespace editor
