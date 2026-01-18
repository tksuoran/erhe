#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <glm/glm.hpp>
#include <imgui/imgui.h>

#include <cstdint>
#include <functional>
#include <string_view>

namespace erhe::window {
    class Key_event;
    class Char_event;
    class Window_focus_event;
    class Cursor_enter_event;
    class Mouse_move_event;
    class Mouse_button_event;
    class Mouse_wheel_event;
}

namespace erhe::imgui {

class Imgui_renderer;
class View;
class Window;

// Base class for derived Imgui_host classes - where ImGui windows can be hosted.
//
// - Current Imgui_host classes are Window_imgui_host and Rendertarget_imgui_host.
// - Every Imgui_window must be hosted in exactly one Imgui_host.
// - Each Imgui_host maintains a separate ImGui context.
// - Each Imgui_host is a Rendergraph_node and as such must implement
//   execute_rendergraph_node() method for rendering ImGui data with
//   Imgui_renderer::render_draw_data().
class Imgui_host : public erhe::rendergraph::Rendergraph_node, public erhe::window::Input_event_handler
{
public:
    Imgui_host(
        erhe::rendergraph::Rendergraph& rendergraph,
        Imgui_renderer&                 imgui_renderer,
        std::string_view                name,
        bool                            imgui_ini,
        ImFontAtlas*                    font_atlas
    );
    ~Imgui_host() noexcept override;

    virtual void begin_imgui_frame  () = 0;
    virtual void process_events     (float dt_s, int64_t time_ns) = 0;
    virtual void end_imgui_frame    () = 0;
    virtual void set_text_input_area(int x, int y, int w, int h) = 0;
    virtual void start_text_input   () = 0;
    virtual void stop_text_input    () = 0;

    void set_begin_callback(const std::function<void(Imgui_host& viewport)>& callback);

    [[nodiscard]] virtual auto is_visible     () const -> bool = 0; // TODO XXX FIX
    [[nodiscard]] virtual auto get_scale_value() const -> float;

    [[nodiscard]] auto name                 () const -> const std::string&;
    [[nodiscard]] auto want_capture_keyboard() const -> bool;
    [[nodiscard]] auto want_capture_mouse   () const -> bool;
    [[nodiscard]] auto has_cursor           () const -> bool;
    [[nodiscard]] auto imgui_context        () const -> ImGuiContext*;

    void update_input_request(bool request_keyboard, bool request_mouse);

    auto on_window_focus_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_cursor_enter_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_key_event         (const erhe::window::Input_event& input_event) -> bool override;
    auto on_text_event        (const erhe::window::Input_event& input_event) -> bool override;
    auto on_char_event        (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_move_event  (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_button_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_wheel_event (const erhe::window::Input_event& input_event) -> bool override;

    [[nodiscard]] auto get_mouse_position() const -> glm::vec2;
    [[nodiscard]] auto get_imgui_renderer() -> Imgui_renderer&;
    [[nodiscard]] auto get_imgui_context () -> ImGuiContext*;

protected:
    std::function<void(Imgui_host& viewport)> m_begin_callback;
    std::string     m_imgui_ini_path;
    bool            m_has_cursor      {false};
    bool            m_request_keyboard{false}; // hovered window requests keyboard events
    bool            m_request_mouse   {false}; // hovered winodw requests mouse events
    Imgui_renderer& m_imgui_renderer;
    ImGuiContext*   m_imgui_context{nullptr};
};

} // namespace erhe::imgui
