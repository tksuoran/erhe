#include "operations/scene_builder_viewport_resources_operation.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "app_settings.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"

namespace editor {

Scene_builder_viewport_resources_operation::Scene_builder_viewport_resources_operation(Parameters parameters)
    : m_parameters{std::move(parameters)}
{
    set_description("Scene_builder viewport resources");
}

Scene_builder_viewport_resources_operation::~Scene_builder_viewport_resources_operation() noexcept = default;

void Scene_builder_viewport_resources_operation::execute(App_context& context)
{
    if (!m_parameters.scene_root || !m_parameters.camera) {
        return;
    }

    Scene_views&  scene_views  = *context.scene_views;
    App_settings& app_settings = *context.app_settings;
    const int     msaa_sample_count = app_settings.graphics.current_graphics_preset.msaa_sample_count;

    std::shared_ptr<erhe::rendergraph::Rendergraph_node> rendergraph_output_node{};
    m_viewport_scene_view = scene_views.create_viewport_scene_view(
        scene_views.get_viewport_config_data(),
        *context.graphics_device,
        *context.rendergraph,
        *context.imgui_windows,
        *context.app_rendering,
        app_settings,
        *context.post_processing,
        *context.tools,
        m_parameters.name,
        m_parameters.scene_root,
        m_parameters.camera,
        std::max(2, msaa_sample_count), //// TODO Fix rendergraph
        rendergraph_output_node,
        m_parameters.enable_post_processing
    );

    if (m_parameters.enable_post_processing) {
        m_post_processing_node = std::dynamic_pointer_cast<Post_processing_node>(rendergraph_output_node);
    } else {
        m_post_processing_node.reset();
    }

    m_shadow_render_node = context.app_rendering->get_shadow_node_for_view(*m_viewport_scene_view);

    m_viewport_window = scene_views.create_viewport_window(
        *context.imgui_renderer,
        *context.imgui_windows,
        m_viewport_scene_view,
        rendergraph_output_node,
        m_parameters.name
    );
}

void Scene_builder_viewport_resources_operation::undo(App_context& context)
{
    if (m_viewport_window) {
        context.scene_views->destroy_viewport_window(m_viewport_window);
        m_viewport_window.reset();
    }
    if (m_shadow_render_node) {
        context.app_rendering->destroy_shadow_node(m_shadow_render_node);
        m_shadow_render_node.reset();
    }
    if (m_viewport_scene_view) {
        context.scene_views->destroy_viewport_scene_view(m_viewport_scene_view, m_post_processing_node);
        m_viewport_scene_view.reset();
        m_post_processing_node.reset();
    }
}

} // namespace editor
