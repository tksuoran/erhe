#pragma once

#include "erhe_rendergraph/sink_rendergraph_node.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::window {
    class Window;
}

namespace editor {

class Viewport_scene_view;
class Scene_views;

class Basic_scene_view_node : public erhe::rendergraph::Sink_rendergraph_node
{
public:
    Basic_scene_view_node(
        erhe::rendergraph::Rendergraph&             rendergraph,
        const std::string_view                      name,
        const std::shared_ptr<Viewport_scene_view>& viewport_scene_view
    );

    // Implements Rendergraph_node
    auto get_consumer_input_viewport    (erhe::rendergraph::Routing routing, int key, int depth = 0) const -> erhe::math::Viewport override;
    auto get_producer_output_viewport   (erhe::rendergraph::Routing routing, int key, int depth = 0) const -> erhe::math::Viewport override;
    auto get_consumer_input_texture     (erhe::rendergraph::Routing routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto get_producer_output_texture    (erhe::rendergraph::Routing routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto get_consumer_input_framebuffer (erhe::rendergraph::Routing routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;
    auto get_producer_output_framebuffer(erhe::rendergraph::Routing routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    // Public API
    [[nodiscard]] auto get_viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view>;
    [[nodiscard]] auto get_viewport           () const -> const erhe::math::Viewport&;
    void set_viewport  (erhe::math::Viewport viewport);
    void set_is_hovered(bool is_hovered);

private:
    // Does *not* directly point to window,
    // because layout is done by Scene_views.
    std::weak_ptr<Viewport_scene_view> m_viewport_scene_view;
    //Scene_views*                     m_viewport_scene_views;
    erhe::math::Viewport               m_viewport;
};

} // namespace editor
