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

    [[nodiscard]] auto get_viewport() const -> erhe::math::Viewport;

    // Implements Imgui_host
    void begin_imgui_frame  ()                            override;
    void process_events     (float dt_s, int64_t time_ns) override;
    void end_imgui_frame    ()                            override;
    auto is_visible         () const -> bool              override;
    void set_text_input_area(int x, int y, int w, int h)  override;
    void start_text_input   ()                            override;
    void stop_text_input    ()                            override;

private:
    erhe::window::Context_window& m_context_window;
    bool                          m_is_visible     {false};
    float                         m_this_frame_dt_s{0.0f};
};

} // namespace erhe::imgui
