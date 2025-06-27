#pragma once

#include "erhe_imgui/imgui_host.hpp"

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_window;
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Rendergraph;
}

namespace editor {

class Editor_context;
class Headset_view;
class Rendertarget_mesh;

class Rendertarget_imgui_host : public erhe::imgui::Imgui_host
{
public:
    Rendertarget_imgui_host(
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_context&                 editor_context,
        Rendertarget_mesh*              rendertarget_mesh,
        const std::string_view          name,
        bool                            imgui_ini = true
    );
    virtual ~Rendertarget_imgui_host() noexcept;

    [[nodiscard]] auto rendertarget_mesh() -> Rendertarget_mesh*;

    // Implements Imgui_host
    auto get_scale_value    () const -> float             override;
    auto is_visible         () const -> bool              override;
    void begin_imgui_frame  ()                            override;
    void process_events     (float dt_s, int64_t time_ns) override;
    void end_imgui_frame    ()                            override;
    void set_text_input_area(int x, int y, int w, int h)  override;
    void start_text_input   ()                            override;
    void stop_text_input    ()                            override;

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;

    // Implements Input_event_handler
#if defined(ERHE_XR_LIBRARY_OPENXR)
    auto on_xr_boolean_event (const erhe::window::Input_event&) -> bool override;
    auto on_xr_float_event   (const erhe::window::Input_event&) -> bool override;
    auto on_xr_vector2f_event(const erhe::window::Input_event&) -> bool override;
#endif

    [[nodiscard]] auto get_mutable_style() -> ImGuiStyle&; // style = imgui_context->Style;
    [[nodiscard]] auto get_style        () const -> const ImGuiStyle&; // style = imgui_context->Style;

    auto get_consumer_input_texture (int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto get_producer_output_texture(int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;

private:
    Editor_context&    m_context;
    Rendertarget_mesh* m_rendertarget_mesh{nullptr};
    float              m_last_mouse_x{0.0f};
    float              m_last_mouse_y{0.0f};
    bool               m_is_visible{false};
    float              m_this_frame_dt_s{0.0};
    int64_t            m_time_ns{};
};

} // namespace editor
