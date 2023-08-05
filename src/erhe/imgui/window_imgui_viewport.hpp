#pragma once

#include "erhe/imgui/imgui_viewport.hpp"
#include "erhe/rendergraph/rendergraph_node.hpp"
#include "erhe/toolkit/viewport.hpp"

namespace erhe::toolkit {
    class Context_window;
}

namespace erhe::imgui
{

class Imgui_renderer;

// Imgui_viewport which displays Imgui_window instance into (glfw) Window.
class Window_imgui_viewport
    : public Imgui_viewport
{
public:
    Window_imgui_viewport(
        Imgui_renderer&                 imgui_renderer,
        erhe::toolkit::Context_window&  context_window,
        erhe::rendergraph::Rendergraph& rendergraph,
        const std::string_view          name
    );
    ~Window_imgui_viewport() override;

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;
    [[nodiscard]] auto get_producer_output_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::toolkit::Viewport override;

    // Implements Imgui_vewport
    [[nodiscard]] auto begin_imgui_frame() -> bool override;

    void end_imgui_frame() override;

private:
    Imgui_renderer&                m_imgui_renderer;
    erhe::toolkit::Context_window& m_context_window;
};

} // namespace erhe::imgui
