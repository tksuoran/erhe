#include "rendergraph/shadow_render_node.hpp"

#include "renderers/shadow_renderer.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"

#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/format.h>

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Shadow_render_node::Shadow_render_node(
    Shadow_renderer&                   shadow_renderer,
    const std::shared_ptr<Scene_view>& scene_view,
    const int                          resolution,
    const int                          light_count,
    const bool                         reverse_depth
)
    : erhe::application::Rendergraph_node{
        "shadow_maps" // TODO fmt::format("Shadow render {}", viewport_window->name())
    }
    , m_shadow_renderer{shadow_renderer}
    , m_scene_view     {scene_view}
{
    register_output(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::application::Rendergraph_node_key::shadow_maps
    );

    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

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

#if 0
        if (erhe::graphics::Instance::info.use_sparse_texture)
        {
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

        m_texture->set_debug_label("Shadowmaps");
        //float depth_clear_value = erhe::graphics::Instance::depth_clear_value;
        //gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
    }

    for (int i = 0; i < light_count; ++i)
    {
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
        .width         = m_texture->width(),
        .height        = m_texture->height(),
        .reverse_depth = reverse_depth  //// m_configuration->graphics.reverse_depth
    };
}

void Shadow_render_node::execute_rendergraph_node()
{
    // Render shadow maps
    const auto& scene_root = m_scene_view->get_scene_root();
    const auto& camera     = m_scene_view->get_camera();
    if (!scene_root || !camera)
    {
        return;
    }

    const auto& layers = scene_root->layers();
    if (layers.content()->meshes.empty())
    {
        return;
    }

    scene_root->sort_lights();

    m_shadow_renderer.render(
        Shadow_renderer::Render_parameters{
            .scene_root            = scene_root.get(),
            .view_camera           = camera.get(),
            ////.view_camera_viewport  = m_viewport_window->projection_viewport(),
            .light_camera_viewport = m_viewport,
            .texture               = *m_texture.get(),
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

[[nodiscard]] auto Shadow_render_node::get_scene_view() const -> std::shared_ptr<Scene_view>
{
    return m_scene_view;
}

[[nodiscard]] auto Shadow_render_node::get_light_projections() -> Light_projections&
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
