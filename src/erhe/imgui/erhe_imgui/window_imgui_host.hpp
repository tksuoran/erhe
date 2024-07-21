#pragma once

#include "erhe_imgui/imgui_host.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_math/viewport.hpp"

namespace erhe::window {
    class Context_window;
}

namespace erhe::imgui {

class Imgui_renderer;

// Imgui_host which displays Imgui_window instance into (glfw) Window.
class Window_imgui_host : public Imgui_host
{
public:
    Window_imgui_host(
        Imgui_renderer&                 imgui_renderer,
        erhe::window::Context_window&   context_window,
        erhe::rendergraph::Rendergraph& rendergraph,
        const std::string_view          name
    );
    ~Window_imgui_host() override;

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;
    auto get_producer_output_viewport(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;

    // Implements Imgui_host
    auto begin_imgui_frame() -> bool override;
    void end_imgui_frame  () override;

private:
    void process_input_events_from_context_window();

    erhe::window::Context_window& m_context_window;
};

} // namespace erhe::imgui
