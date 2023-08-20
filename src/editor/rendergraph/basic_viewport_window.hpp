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

namespace editor
{

class Viewport_window;
class Viewport_windows;

class Basic_viewport_window
    : public erhe::rendergraph::Sink_rendergraph_node
{
public:
    Basic_viewport_window(
        erhe::rendergraph::Rendergraph&         rendergraph,
        const std::string_view                  name,
        const std::shared_ptr<Viewport_window>& viewport_window
    );

    // Implements Rendergraph_node
    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::math::Viewport override;

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::math::Viewport override;

    [[nodiscard]] auto get_consumer_input_texture(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_producer_output_texture(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_consumer_input_framebuffer(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    [[nodiscard]] auto get_producer_output_framebuffer(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    // Public API
    [[nodiscard]] auto get_viewport_window() const -> std::shared_ptr<Viewport_window>;
    [[nodiscard]] auto get_viewport       () const -> const erhe::math::Viewport&;
    void set_viewport  (erhe::math::Viewport viewport);
    void set_is_hovered(bool is_hovered);

private:
    // Does *not* directly point to window,
    // because layout is done by Viewport_windows.
    std::weak_ptr<Viewport_window> m_viewport_window;
    //Viewport_windows*              m_viewport_windows;
    erhe::math::Viewport           m_viewport;
};

} // namespace editor
