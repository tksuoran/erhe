#include "rendergraph/viewport_overlay_node.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_graphics/texture.hpp"
#include "erhe_rendergraph/rendergraph.hpp"

namespace editor {

Viewport_overlay_node::Viewport_overlay_node(
    erhe::rendergraph::Rendergraph&             rendergraph,
    const std::shared_ptr<Viewport_scene_view>& viewport_scene_view,
    erhe::utility::Debug_label                  debug_label
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, debug_label}
    , m_viewport_scene_view{viewport_scene_view}
{
    register_input ("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    register_output("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
}

void Viewport_overlay_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    const std::shared_ptr<erhe::graphics::Texture> input_texture =
        get_consumer_input_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    if (!input_texture) {
        return;
    }
    viewport_scene_view->render_overlay_pass(command_buffer, input_texture);
}

auto Viewport_overlay_node::get_producer_output_texture(const int key, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    if ((key == erhe::rendergraph::Rendergraph_node_key::viewport_texture) ||
        (key == erhe::rendergraph::Rendergraph_node_key::wildcard)) {
        const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
        if (viewport_scene_view) {
            return viewport_scene_view->get_overlay_output_texture();
        }
    }
    return {};
}

}
