#pragma once

#include "erhe_imgui/imgui_host.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_math/viewport.hpp"

#include <memory>

namespace erhe::graphics {
    class Device;
    class Render_pass;
    class Swapchain;
}
namespace erhe::window   { class Context_window; }

namespace erhe::imgui {

class Imgui_renderer;

// Imgui_host which displays Imgui_window instance into (glfw) Window.
class Window_imgui_host : public Imgui_host
{
public:
    Window_imgui_host(
        Imgui_renderer&                 imgui_renderer,
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Swapchain&      swapchain,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::window::Context_window&   context_window,
        const std::string_view          name
    );
    ~Window_imgui_host() override;

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;

    [[nodiscard]] auto get_viewport() const -> erhe::math::Viewport;
    void set_status_bar_callback(const std::function<void(Window_imgui_host& host)>& callback);

    // Implements Imgui_host
    void begin_imgui_frame  ()                            override;
    void process_events     (float dt_s, int64_t time_ns) override;
    void end_imgui_frame    ()                            override;
    auto is_visible         () const -> bool              override;
    void set_text_input_area(int x, int y, int w, int h)  override;
    void start_text_input   ()                            override;
    void stop_text_input    ()                            override;

private:
    void update_render_pass(int width, int height);

    erhe::window::Context_window&                m_context_window;
    erhe::graphics::Device&                      m_graphics_device;
    erhe::graphics::Swapchain&                   m_swapchain;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
    bool                                         m_is_visible         {false};
    float                                        m_this_frame_dt_s    {0.0f};
    std::function<void(Window_imgui_host& host)> m_status_bar_callback{};
};

} // namespace erhe::imgui
