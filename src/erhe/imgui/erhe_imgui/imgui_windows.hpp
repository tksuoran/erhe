#pragma once

#include "erhe_window/window_event_handler.hpp"
#include "erhe_profile/profile.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Swapchain;
}
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
        erhe::graphics::Device&         graphics_device,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::window::Context_window*   context_window,
        std::string_view                windows_ini_path
    );

    // Public API
    void lock_mutex             ();
    void unlock_mutex           ();
    void queue                  (std::function<void()>&& operation);
    void flush_queue            ();
    void register_imgui_window  (Imgui_window* window);
    void unregister_imgui_window(Imgui_window* window);

    void begin_frame            ();
    void process_events         (float dt_s, int64_t time_ns);
    void end_frame              ();
    void draw_imgui_windows     ();

    void window_menu_entries    (Imgui_host& imgui_host, bool developer);
    auto get_windows            () -> std::vector<Imgui_window*>&;
    void save_window_state      ();

    [[nodiscard]] auto get_window_imgui_host     () -> std::shared_ptr<Window_imgui_host>;
    [[nodiscard]] auto want_capture_keyboard     () const -> bool;
    [[nodiscard]] auto want_capture_mouse        () const -> bool;
    [[nodiscard]] auto get_persistent_window_open(std::string_view ini_label) const -> bool;

    // Implements Input_event_han
    // Forwards events to each window
    auto on_key_event         (const erhe::window::Input_event& input_event) -> bool override;
    auto on_char_event        (const erhe::window::Input_event& input_event) -> bool override;
    auto on_window_focus_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_cursor_enter_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_move_event  (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_button_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_wheel_event (const erhe::window::Input_event& input_event) -> bool override;

    void debug_imgui();

private:
    Imgui_renderer&                    m_imgui_renderer;
    erhe::window::Context_window*      m_context_window{nullptr};
    std::recursive_mutex               m_mutex;
    bool                               m_iterating{false};
    std::vector<Imgui_window*>         m_imgui_windows;
    ERHE_PROFILE_MUTEX(std::mutex,     m_queued_operations_mutex);
    std::vector<std::function<void()>> m_queued_operations;
    erhe::rendergraph::Rendergraph&    m_rendergraph;
    std::string                        m_windows_ini_path;

    std::shared_ptr<Window_imgui_host> m_window_imgui_host;
};

} // namespace erhe::imgui
