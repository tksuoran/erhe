#include "rendergraph/shadow_render_node.hpp"
#include "renderers/mesh_memory.hpp"
#include "editor_log.hpp"

#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/renderer/shadow_renderer.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/format.h>

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;


Shadow_render_node::Shadow_render_node(
    Scene_view& scene_view,
    const int   resolution,
    const int   light_count,
    const bool  reverse_depth
)
    : erhe::application::Rendergraph_node{
        "shadow_maps" // TODO fmt::format("Shadow render {}", viewport_window->name())
    }
    , m_scene_view{scene_view}
{
    register_output(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::application::Rendergraph_node_key::shadow_maps
    );

    reconfigure(resolution, light_count, reverse_depth);
}

void Shadow_render_node::reconfigure(
    const int  resolution,
    const int  light_count,
    const bool reverse_depth
)
{
    log_render->info("Reconfigure shadow resolution = {}, light count = {}", resolution, light_count);
    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

        m_texture.reset();

        if (light_count > 0) {
            m_texture = std::make_shared<Texture>(
                Texture::Create_info
                {
                    .target          = gl::Texture_target::texture_2d_array,
                    .internal_format = gl::Internal_format::depth_component32f,
                    //.sparse          = erhe::graphics::Instance::info.use_sparse_texture,
                    .width           = resolution,
                    .height          = resolution,
                    .depth           = light_count
                }
            );

            m_texture->set_debug_label("Shadowmaps");
        }
        if (resolution == 1) {
            float depth_clear_value = reverse_depth ? 0.0f : 1.0f;
            if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
                gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
            } else {
                // TODO
            }
        }
    }

    m_framebuffers.clear();
    for (int i = 0; i < light_count; ++i) {
        ERHE_PROFILE_SCOPE("framebuffer creation");

        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::depth_attachment, m_texture.get(), 0, static_cast<unsigned int>(i));
        auto framebuffer = std::make_unique<Framebuffer>(create_info);
        framebuffer->set_debug_label(fmt::format("Shadow {}", i));
        m_framebuffers.emplace_back(std::move(framebuffer));
    }

    m_viewport = {
        .x             = 0,
        .y             = 0,
        .width         = resolution,
        .height        = resolution,
        .reverse_depth = reverse_depth
    };
}

void Shadow_render_node::execute_rendergraph_node()
{
    // Render shadow maps
    const auto& scene_root = m_scene_view.get_scene_root();
    const auto& camera     = m_scene_view.get_camera();
    if (!scene_root || !camera) {
        return;
    }

    const auto& layers = scene_root->layers();
    if (layers.content()->meshes.empty()) {
        return;
    }

    if (!m_texture) {
        return;
    }

    scene_root->sort_lights();

    erhe::renderer::g_shadow_renderer->render(
        erhe::renderer::Shadow_renderer::Render_parameters{
            .vertex_input_state    = g_mesh_memory->get_vertex_input(),
            .index_type            = g_mesh_memory->gl_index_type(),

            .view_camera           = camera.get(),
            ////.view_camera_viewport  = m_viewport_window->projection_viewport(),
            .light_camera_viewport = m_viewport,
            .texture               = m_texture,
            .framebuffers          = m_framebuffers,
            .mesh_spans            = { layers.content()->meshes },
            .lights                = layers.light()->lights,
            .light_projections     = m_light_projections
        }
    );
}

[[nodiscard]] auto Shadow_render_node::get_producer_output_texture(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    int                                       depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return m_texture;
}

[[nodiscard]] auto Shadow_render_node::get_producer_output_viewport(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::scene::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(depth);
    ERHE_VERIFY(key == erhe::application::Rendergraph_node_key::shadow_maps);
    return m_viewport;
}

[[nodiscard]] auto Shadow_render_node::get_scene_view() -> Scene_view&
{
    return m_scene_view;
}

[[nodiscard]] auto Shadow_render_node::get_scene_view() const -> const Scene_view&
{
    return m_scene_view;
}

[[nodiscard]] auto Shadow_render_node::get_light_projections() -> erhe::renderer::Light_projections&
{
    return m_light_projections;
}

auto Shadow_render_node::get_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

auto Shadow_render_node::get_viewport() const -> erhe::scene::Viewport
{
    return m_viewport;
}

[[nodiscard]] auto Shadow_render_node::inputs_allowed() const -> bool
{
    return false;
}

} // namespace editor



#if 0
        if (erhe::graphics::Instance::info.use_sparse_texture) {
            // commit the whole texture for now
            gl::texture_page_commitment_ext(
                m_texture->gl_name(),
                0,                  // level
                0,                  // x offset
                0,                  // y offset,
                0,                  // z offset
                m_texture->width(),
                m_texture->height(),
                m_texture->depth(),
                GL_TRUE
            );
        }
#endif
