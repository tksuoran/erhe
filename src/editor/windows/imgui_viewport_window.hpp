#pragma once

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::imgui {
    class Imgui_viewport;
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Multisample_resolve_node;
    class Rendergraph_node;
}

namespace editor {

class Post_processing_node;
class Viewport_window;

// Rendergraph sink node for showing contents originating from Viewport_window
class Imgui_viewport_window
    : public erhe::imgui::Imgui_window
    , public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Imgui_viewport_window(
        erhe::imgui::Imgui_renderer&            imgui_renderer,
        erhe::imgui::Imgui_windows&             imgui_windows,
        erhe::rendergraph::Rendergraph&         rendergraph,
        const std::string_view                  name,
        const std::string_view                  ini_label,
        const std::shared_ptr<Viewport_window>& viewport_window
    );

    // Implements Imgui_window
    void imgui               () override;
    void hidden              () override;
    auto has_toolbar         () const -> bool override { return true; }
    void toolbar             (bool& hovered) override;
    void on_begin            () override;
    void on_end              () override;
    void set_viewport        (erhe::imgui::Imgui_viewport* imgui_viewport) override;
    auto want_mouse_events   () const -> bool override;
    auto want_keyboard_events() const -> bool override;
    void on_mouse_move       (glm::vec2 mouse_position_in_window);

    // Implements Rendergraph_node
    auto get_consumer_input_viewport(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;

    // Overridden to source viewport size from ImGui window
    auto get_producer_output_viewport(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;

    // Public API
    [[nodiscard]] auto viewport_window() const -> std::shared_ptr<Viewport_window>;
    [[nodiscard]] auto is_hovered     () const -> bool;

private:
    std::weak_ptr<Viewport_window> m_viewport_window;
    erhe::math::Viewport           m_viewport;
};

} // namespace editor
