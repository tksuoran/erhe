#include "rendergraph/basic_scene_view_node.hpp"

#include "scene/viewport_scene_view.hpp"

namespace editor {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

Basic_scene_view_node::Basic_scene_view_node(
    erhe::rendergraph::Rendergraph&             rendergraph,
    const std::string_view                      name,
    const std::shared_ptr<Viewport_scene_view>& viewport_scene_view
)
    : erhe::rendergraph::Sink_rendergraph_node{rendergraph, std::string{name}}
    , m_viewport_scene_view{viewport_scene_view}
{
    // Initially empty viewport. Layout is done by Scene_views
    m_viewport.x      = 0;
    m_viewport.y      = 0;
    m_viewport.width  = 0;
    m_viewport.height = 0;

    register_input(
        erhe::rendergraph::Routing::Resource_provided_by_consumer,
        "viewport",
        erhe::rendergraph::Rendergraph_node_key::viewport
    );

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Imgui_window_scene_view a dependency for Imgui_host, forcing
    // correct rendering order (Imgui_window_scene_view must be rendered before
    // Imgui_host).
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_output(
        erhe::rendergraph::Routing::None,
        "window",
        erhe::rendergraph::Rendergraph_node_key::window
    );
}

auto Basic_scene_view_node::get_viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view>
{
    return m_viewport_scene_view.lock();
}

auto Basic_scene_view_node::get_consumer_input_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return get_viewport();
}

auto Basic_scene_view_node::get_producer_output_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return get_viewport();
}

auto Basic_scene_view_node::get_consumer_input_texture(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return {};
}

auto Basic_scene_view_node::get_producer_output_texture(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return {};
}

auto Basic_scene_view_node::get_consumer_input_render_pass(erhe::rendergraph::Routing, int, int) const -> erhe::graphics::Render_pass*
{
    return nullptr;
}

auto Basic_scene_view_node::get_producer_output_render_pass(erhe::rendergraph::Routing, int, int) const -> erhe::graphics::Render_pass*
{
    return nullptr;
}

auto Basic_scene_view_node::get_viewport() const -> const erhe::math::Viewport&
{
    return m_viewport;
}

void Basic_scene_view_node::set_viewport(erhe::math::Viewport viewport)
{
    m_viewport = viewport;

    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (viewport_scene_view) {
        viewport_scene_view->set_window_viewport(viewport);
    }
}

void Basic_scene_view_node::set_is_hovered(const bool is_hovered)
{
    const auto viewport_scene_view = m_viewport_scene_view.lock();
    if (viewport_scene_view) {
        viewport_scene_view->set_is_scene_view_hovered(is_hovered);
    }
}

} // namespace editor
