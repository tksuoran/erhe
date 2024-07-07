#pragma once

#include "erhe_window/window_event_handler.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::window {
    class Context_window;
}

namespace erhe::imgui
{

class Imgui_window;
class Imgui_host;
class Imgui_renderer;
class Window_imgui_host;

class Imgui_windows : public erhe::window::Window_event_handler
{
public:
    // Implements Window_event_handler
    auto get_name() const -> const char* override { return "Imgui_windows"; }

    Imgui_windows(
        Imgui_renderer&                 imgui_renderer,
        erhe::window::Context_window*   context_window,
        erhe::rendergraph::Rendergraph& rendergraph
    );

    // Public API
    void lock_mutex             ();
    void unlock_mutex           ();
    void queue                  (std::function<void()>&& operation);
    void flush_queue            ();
    void register_imgui_window  (Imgui_window* window);
    void unregister_imgui_window(Imgui_window* window);
    void imgui_windows          ();
    void window_menu_entries    (Imgui_host& imgui_host);
    auto get_windows            () -> std::vector<Imgui_window*>&;
    void save_window_state      ();

    [[nodiscard]] auto get_window_imgui_host() -> std::shared_ptr<Window_imgui_host>;
    [[nodiscard]] auto want_capture_keyboard() const -> bool;
    [[nodiscard]] auto want_capture_mouse   () const -> bool;

    // Implements erhe::window::Window_event_handler
    // Forwards events to each window
    auto on_key         (signed int keycode, uint32_t modifier_mask, bool pressed) -> bool override;
    auto on_char        (unsigned int codepoint) -> bool                                   override;
    auto on_focus       (int focused) -> bool                                              override;
    auto on_cursor_enter(int entered) -> bool                                              override;
    auto on_mouse_move  (float absolute_x, float absolute_y, float relative_x, float relative_y, uint32_t modifier_mask) -> bool override;
    auto on_mouse_button(uint32_t button, bool pressed, uint32_t modifier_mask) -> bool    override;
    auto on_mouse_wheel (float x, float y, uint32_t modifier_mask) -> bool                 override;

private:
    Imgui_renderer&                    m_imgui_renderer;
    std::recursive_mutex               m_mutex;
    bool                               m_iterating{false};
    std::vector<Imgui_window*>         m_imgui_windows;
    std::mutex                         m_queued_operations_mutex;
    std::vector<std::function<void()>> m_queued_operations;
    erhe::rendergraph::Rendergraph&    m_rendergraph;

    std::shared_ptr<Window_imgui_host> m_window_imgui_host;
};

} // namespace erhe::imgui
