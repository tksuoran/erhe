#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_utility/debug_label.hpp"

#include <memory>
#include <string_view>

namespace erhe::graphics {
    class Command_buffer;
    class Texture;
}
namespace erhe::rendergraph {
    class Rendergraph;
}

namespace editor {

class Viewport_scene_view;

// Rendergraph node placed AFTER the post-processing node for a viewport. It
// draws the overlay (tool gizmo meshes + hotbar / rendertarget meshes) on top
// of the post-processed image, depth-testing against the content render
// target's stored depth/stencil attachment, so the overlay is unaffected by
// camera exposure and post-processing. See issue #230.
//
// The node itself is thin: the overlay render target, the fullscreen composite
// of the post-processed input, and the overlay composition passes all live on
// the Viewport_scene_view (which owns the content render context). This node
// only forwards the post-processing output texture into
// Viewport_scene_view::render_overlay_pass and exposes the result.
class Viewport_overlay_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Viewport_overlay_node(
        erhe::rendergraph::Rendergraph&             rendergraph,
        const std::shared_ptr<Viewport_scene_view>& viewport_scene_view,
        erhe::utility::Debug_label                  debug_label
    );

    // Implements Rendergraph_node
    auto get_type_name           () const -> std::string_view override { return "Viewport_overlay_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;
    auto get_producer_output_texture(int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;

    // The viewport this overlay belongs to (used by Scene_views teardown to
    // drop the matching overlay node). May be expired during shutdown.
    [[nodiscard]] auto get_viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view> { return m_viewport_scene_view.lock(); }

private:
    // Weak, to avoid keeping the Viewport_scene_view alive through the
    // rendergraph node graph.
    std::weak_ptr<Viewport_scene_view> m_viewport_scene_view;
};

}
