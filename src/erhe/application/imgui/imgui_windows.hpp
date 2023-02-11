#pragma once

#include "erhe/components/components.hpp"

namespace erhe::application
{

class Imgui_window;
class Imgui_viewport;
class Window_imgui_viewport;

class Imgui_builtin_windows
{
public:
    bool demo        {false};
    bool style_editor{false};
    bool metrics     {false};
    bool stack_tool  {false};
};

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
    [[nodiscard]] auto get_mutex                () -> std::mutex&;
    [[nodiscard]] auto get_window_viewport      () -> std::shared_ptr<Window_imgui_viewport>;
    [[nodiscard]] auto get_imgui_builtin_windows() -> Imgui_builtin_windows&;
    void register_imgui_viewport (const std::shared_ptr<Imgui_viewport>& viewport);
    void register_imgui_window   (Imgui_window* window, const char* ini_entry);
    void register_imgui_window   (Imgui_window* window, bool visible);
    void make_current            (const Imgui_viewport* imgui_viewport);
    void imgui_windows           ();
    void window_menu             (Imgui_viewport* imgui_viewport);
    auto get_windows             () -> std::vector<Imgui_window*>&;

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
    std::mutex                                   m_mutex;
    std::vector<std::shared_ptr<Imgui_viewport>> m_imgui_viewports;
    std::vector<Imgui_window*>                   m_imgui_windows;
    const Imgui_viewport*                        m_current_viewport{nullptr}; // current context
    std::shared_ptr<Window_imgui_viewport>       m_window_imgui_viewport;
    Imgui_builtin_windows                        m_imgui_builtin_windows;
};

extern Imgui_windows* g_imgui_windows;

} // namespace erhe::application
