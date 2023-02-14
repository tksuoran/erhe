#pragma once

#include "erhe/components/components.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::application
{

class Imgui_window;
class Imgui_viewport;
class Window_imgui_viewport;

/// <summary>
/// Maintains collection of Imgui_windows and Imgui_viewports
/// </summary>
/// Maintains current Imgui_viewport that ha active ImGui context.
/// All Imgui_window instances must be registered to Imgui_windows.
/// Imgui_windows delegates input events to Imgui_viewport instances.
class Imgui_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Imgui_windows"};
    static constexpr std::string_view c_title{"ImGui Windows"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Imgui_windows ();
    ~Imgui_windows() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    void lock_mutex             ();
    void unlock_mutex           ();
    void queue                  (std::function<void()>&& operation);
    void flush_queue            ();
    void register_imgui_viewport(const std::shared_ptr<Imgui_viewport>& viewport);
    void register_imgui_window  (Imgui_window* window, const char* ini_entry);
    void register_imgui_window  (Imgui_window* window, bool visible);
    void make_current           (const Imgui_viewport* imgui_viewport);
    void imgui_windows          ();
    void window_menu            (Imgui_viewport* imgui_viewport);
    auto get_windows            () -> std::vector<Imgui_window*>&;

    [[nodiscard]] auto get_window_viewport  () -> std::shared_ptr<Window_imgui_viewport>;
    [[nodiscard]] auto want_capture_keyboard() const -> bool;
    [[nodiscard]] auto want_capture_mouse   () const -> bool;

    // NOTE: Same interface as Imgui_viewport
    // Forwards events to each window
    void on_key         (signed int keycode, uint32_t modifier_mask, bool pressed);
    void on_char        (unsigned int codepoint);
    void on_focus       (int focused);
    void on_cursor_enter(int entered);
    void on_mouse_move  (float x, float y);
    void on_mouse_button(uint32_t button, bool pressed);
    void on_mouse_wheel (float x, float y);

private:
    std::recursive_mutex                         m_mutex;
    bool                                         m_iterating{false};
    std::vector<std::shared_ptr<Imgui_viewport>> m_imgui_viewports;
    std::vector<Imgui_window*>                   m_imgui_windows;
    std::mutex                                   m_queued_operations_mutex;
    std::vector<std::function<void()>>           m_queued_operations;
    const Imgui_viewport*                        m_current_viewport{nullptr}; // current context
    std::shared_ptr<Window_imgui_viewport>       m_window_imgui_viewport;
};

extern Imgui_windows* g_imgui_windows;

} // namespace erhe::application
