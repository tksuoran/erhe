#include "rendergraph/shadow_render_node.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

namespace editor {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

Shadow_render_node::Shadow_render_node(
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    App_context&                    context,
    Scene_view&                     scene_view,
    const int                       resolution,
    const int                       light_count,
    const int                       depth_bits
)
    // TODO fmt::format("Shadow render {}", viewport_scene_view->name())
    : erhe::rendergraph::Rendergraph_node{rendergraph, "shadow_maps"} 
    , m_context   {context}
    , m_scene_view{scene_view}
{
    register_output("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);

    reconfigure(graphics_device, resolution, light_count, depth_bits);
}

Shadow_render_node::~Shadow_render_node() noexcept
{
}

void Shadow_render_node::reconfigure(erhe::graphics::Device& graphics_device, const int resolution, const int light_count, const int requested_depth_bits)
{
    std::vector<erhe::dataformat::Format> formats = graphics_device.get_supported_depth_stencil_formats();
    std::stable_sort(
        formats.begin(),
        formats.end(),
        [&](const erhe::dataformat::Format& lhs, const erhe::dataformat::Format& rhs)
        {
            const size_t lhs_format_size_bytes = erhe::dataformat::get_format_size_bytes(lhs);
            const size_t rhs_format_size_bytes = erhe::dataformat::get_format_size_bytes(rhs);
            const int    lhs_depth_size_bits   = static_cast<int>(erhe::dataformat::get_depth_size_bits(lhs));
            const int    rhs_depth_size_bits   = static_cast<int>(erhe::dataformat::get_depth_size_bits(rhs));
            const int    lhs_depth_diff        = lhs_depth_size_bits - requested_depth_bits;
            const int    rhs_depth_diff        = rhs_depth_size_bits - requested_depth_bits;
            const int    lhs_abs_diff          = std::abs(lhs_depth_diff);
            const int    rhs_abs_diff          = std::abs(rhs_depth_diff);
            bool result;
            if (lhs_depth_size_bits == rhs_depth_size_bits) {
                return lhs_format_size_bytes < rhs_format_size_bytes;
            } else if ((lhs_depth_diff > 0) && (lhs_depth_diff < 0)) {
                result = true;
            } else if ((rhs_depth_diff > 0) && (lhs_depth_diff < 0)) {
                result = false;
            } else if (lhs_depth_size_bits == 0) {
                result = false;
            } else if (rhs_depth_size_bits == 0) {
                result = true;
            } else {
                result = (lhs_abs_diff < rhs_abs_diff);
            }
            return result;
        }
    );
    const erhe::dataformat::Format depth_format = graphics_device.choose_depth_stencil_format(formats);
    if (depth_format == erhe::dataformat::Format::format_undefined) {
        log_render->error(
            "Reconfigure shadow resolution = {}, light count = {}, no depth format found, skip",
            resolution, light_count
        );
        m_texture.reset();
        return;
    }

    if (
        m_texture &&
        (m_texture->get_width()             == resolution) &&
        (m_texture->get_height()            == resolution) &&
        (m_texture->get_array_layer_count() == std::max(1, light_count)) &&
        (m_texture->get_pixelformat()       == depth_format)
    ) {
        log_render->debug(
            "Reconfigure shadow resolution = {}, light count = {}, format = {} - match old settings, skip",
            resolution, light_count, erhe::dataformat::c_str(depth_format)
        );
        return;
    }
    log_render->debug("Reconfigure shadow resolution = {}, light count = {}", resolution, light_count);

    //// TODO device.wait_for_idle()

    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

        m_texture.reset();
        m_texture = std::make_shared<Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info {
                .device            = graphics_device,
                .usage_mask        =
                    erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::sampled,
                .type              = erhe::graphics::Texture_type::texture_2d,
                .pixelformat       = depth_format,
                .width             = std::max(1, resolution),
                .height            = std::max(1, resolution),
                .depth             = 1,
                .array_layer_count = std::max(1, light_count),
                .debug_label       = "Shadowmap"
            }
        );

        if (resolution <= 1) {
            double depth_clear_value = 0.0; // reverse Z
            graphics_device.clear_texture(*m_texture.get(), { depth_clear_value, 0.0, 0.0, 0.0 });
        }
    }

    log_render->debug("updating render passes");
    m_render_passes.clear();
    for (int i = 0; i < light_count; ++i) {
        erhe::graphics::Render_pass_descriptor render_pass_descriptor;
        render_pass_descriptor.depth_attachment.texture        = m_texture.get();
        render_pass_descriptor.depth_attachment.texture_level  = 0;
        render_pass_descriptor.depth_attachment.texture_layer  = static_cast<unsigned int>(i);
        render_pass_descriptor.depth_attachment.load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.depth_attachment.store_action   = erhe::graphics::Store_action::Store;
        render_pass_descriptor.depth_attachment.clear_value[0] = 0.0; // Reverse Z
        render_pass_descriptor.render_target_width             = resolution;
        render_pass_descriptor.render_target_height            = resolution;
        render_pass_descriptor.debug_label                     = fmt::format("Shadow {}", i);
        std::unique_ptr<erhe::graphics::Render_pass> render_pass = std::make_unique<Render_pass>(graphics_device, render_pass_descriptor);
        m_render_passes.emplace_back(std::move(render_pass));
    }

    m_viewport = {
        .x      = 0,
        .y      = 0,
        .width  = resolution,
        .height = resolution
    };

    // Invalidate m_light_projections which at this point has stale texture handles
    m_light_projections = erhe::scene_renderer::Light_projections{};
}

void Shadow_render_node::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

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

    // TODO: Keep this vector in content library and update when needed.
    //       Make content library item host for content library nodes.
    const auto& material_library = scene_root->get_content_library()->materials;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials = material_library->get_all<erhe::primitive::Material>();

    scene_root->sort_lights();

    m_context.shadow_renderer->render(
        erhe::scene_renderer::Shadow_renderer::Render_parameters{
            .vertex_input_state    = &m_context.mesh_memory->vertex_input,
            .index_type            = m_context.mesh_memory->buffer_info.index_type,
            .index_buffer          = &m_context.mesh_memory->index_buffer,
            .vertex_buffer         = &m_context.mesh_memory->position_vertex_buffer,
            .view_camera           = camera.get(),
            .view_camera_viewport  = {},
            .light_camera_viewport = m_viewport,
            .texture               = m_texture,
            .render_passes         = m_render_passes,
            .mesh_spans            = { layers.content()->meshes },
            .lights                = layers.light()->lights,
            .skins                 = scene_root->get_scene().get_skins(),
            .materials             = materials,
            .light_projections     = m_light_projections
        }
    );
}

auto Shadow_render_node::get_producer_output_texture(const int key, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    if (key == erhe::rendergraph::Rendergraph_node_key::shadow_maps) {
        return m_texture;
    }
    return std::shared_ptr<erhe::graphics::Texture>{};
}

auto Shadow_render_node::get_scene_view() -> Scene_view&
{
    return m_scene_view;
}

auto Shadow_render_node::get_scene_view() const -> const Scene_view&
{
    return m_scene_view;
}

auto Shadow_render_node::get_light_projections() -> erhe::scene_renderer::Light_projections&
{
    return m_light_projections;
}

auto Shadow_render_node::get_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

auto Shadow_render_node::get_viewport() const -> erhe::math::Viewport
{
    return m_viewport;
}

auto Shadow_render_node::inputs_allowed() const -> bool
{
    return false;
}

}
