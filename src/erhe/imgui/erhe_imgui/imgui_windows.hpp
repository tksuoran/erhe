#pragma once

#include "erhe_window/window_event_handler.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::window {
    class Context_window;
    class Input_event;
}

namespace erhe::imgui {

class Imgui_window;
class Imgui_host;
class Imgui_renderer;
class Window_imgui_host;

class Imgui_windows : public erhe::window::Input_event_handler
{
public:
    Imgui_windows(
        Imgui_renderer&                 imgui_renderer,
        erhe::window::Context_window*   context_window,
        erhe::rendergraph::Rendergraph& rendergraph,
        std::string_view                windows_ini_path
    );

    // Public API
    void lock_mutex             ();
    void unlock_mutex           ();
    void queue                  (std::function<void()>&& operation);
    void flush_queue            ();
    void register_imgui_window  (Imgui_window* window);
    void unregister_imgui_window(Imgui_window* window);
    void imgui_windows          ();
    void window_menu_entries    (Imgui_host& imgui_host, bool developer);
    auto get_windows            () -> std::vector<Imgui_window*>&;
    void save_window_state      ();

    [[nodiscard]] auto get_window_imgui_host     () -> std::shared_ptr<Window_imgui_host>;
    [[nodiscard]] auto want_capture_keyboard     () const -> bool;
    [[nodiscard]] auto want_capture_mouse        () const -> bool;
    [[nodiscard]] auto get_persistent_window_open(std::string_view ini_label) const -> bool;

    // Implements Input_event_han
    // Forwards events to each window
    auto on_key_event         (const erhe::window::Key_event&          key_event         ) -> bool override;
    auto on_char_event        (const erhe::window::Char_event&         char_event        ) -> bool override;
    auto on_window_focus_event(const erhe::window::Window_focus_event& window_focus_event) -> bool override;
    auto on_cursor_enter_event(const erhe::window::Cursor_enter_event& cursor_enter_event) -> bool override;
    auto on_mouse_move_event  (const erhe::window::Mouse_move_event&   mouse_move_event  ) -> bool override;
    auto on_mouse_button_event(const erhe::window::Mouse_button_event& mouse_button_event) -> bool override;
    auto on_mouse_wheel_event (const erhe::window::Mouse_wheel_event&  mouse_wheel_event ) -> bool override;

    void debug_imgui();

private:
    Imgui_renderer&                    m_imgui_renderer;
    std::recursive_mutex               m_mutex;
    bool                               m_iterating{false};
    std::vector<Imgui_window*>         m_imgui_windows;
    std::mutex                         m_queued_operations_mutex;
    std::vector<std::function<void()>> m_queued_operations;
    erhe::rendergraph::Rendergraph&    m_rendergraph;
    std::string                        m_windows_ini_path;

    std::shared_ptr<Window_imgui_host> m_window_imgui_host;
};

} // namespace erhe::imgui
