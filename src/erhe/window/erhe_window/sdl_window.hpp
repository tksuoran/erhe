#pragma once

#include "erhe_window/window_event_handler.hpp"
#include "erhe_window/window_configuration.hpp"

#include <glm/glm.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#if defined(ERHE_OS_WINDOWS)
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   define VC_EXTRALEAN
#   ifndef STRICT
#       define STRICT
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#endif

#if defined(ERHE_OS_LINUX)
struct wl_display;
#endif

namespace erhe::window {

using Mouse_cursor = signed int;
constexpr Mouse_cursor Mouse_cursor_None       = -1;
constexpr Mouse_cursor Mouse_cursor_Arrow      =  0;
constexpr Mouse_cursor Mouse_cursor_TextInput  =  1;   // When hovering over InputText, etc.
constexpr Mouse_cursor Mouse_cursor_ResizeAll  =  2;   // (Unused by Dear ImGui functions)
constexpr Mouse_cursor Mouse_cursor_ResizeNS   =  3;   // When hovering over an horizontal border
constexpr Mouse_cursor Mouse_cursor_ResizeEW   =  4;   // When hovering over a vertical border or a column
constexpr Mouse_cursor Mouse_cursor_ResizeNESW =  5;   // When hovering over the bottom-left corner of a window
constexpr Mouse_cursor Mouse_cursor_ResizeNWSE =  6;   // When hovering over the bottom-right corner of a window
constexpr Mouse_cursor Mouse_cursor_Hand       =  7;   // (Unused by Dear ImGui functions. Use for e.g. hyperlinks)
constexpr Mouse_cursor Mouse_cursor_NotAllowed =  8;   // When hovering something with disallowed interaction. Usually a crossed circle.
constexpr Mouse_cursor Mouse_cursor_Crosshair  =  9;   // Crosshair cursor
constexpr Mouse_cursor Mouse_cursor_COUNT      = 10;

typedef void (*SDL_FunctionPointer)(void);

class Context_window
{
public:
    explicit Context_window(const Window_configuration& configuration);

    explicit Context_window(Context_window* share);
    virtual ~Context_window() noexcept;

    [[nodiscard]] auto get_width               () const -> int;
    [[nodiscard]] auto get_height              () const -> int;
    [[nodiscard]] auto get_cursor_relative_hold() const -> bool;
    [[nodiscard]] auto get_sdl_window          () const -> void* { return m_sdl_window; }
    [[nodiscard]] auto get_input_events        () -> std::vector<Input_event>&;

    void register_redraw_callback(std::function<void()> callback);

    auto open                             (const Window_configuration& configuration) -> bool;

    void poll_events                      (float wait_time = 0.0f);
    void get_cursor_position              (float& xpos, float& ypos);
    void get_cursor_relative_hold_position(float& xpos, float& ypos);
    void set_visible                      (bool visible);
    void set_cursor                       (Mouse_cursor cursor);
    void set_cursor_relative_hold         (bool relative_hold_enabled);
    void set_text_input_area              (int x, int y, int w, int h);
    void start_text_input                 ();
    void stop_text_input                  ();
    void handle_key_event                 (int64_t timestamp, int key, int scancode, bool pressed, int modifiers);
    void handle_text_event                (int64_t timestamp, const char* utf8_text);
    void handle_mouse_button_event        (int64_t timestamp, int button, bool pressed);
    void handle_mouse_wheel_event         (int64_t timestamp, float x, float y);
    void handle_mouse_move                (int64_t timestamp, float x, float y, float dx, float dy);
    void handle_controller_axis_event     (int64_t timestamp, int device, int axis, int value);
    void handle_controller_button_event   (int64_t timestamp, int device, int button, bool pressed);
    void handle_window_resize_event       (int64_t timestamp, int width, int height);
    void handle_window_refresh_event      (int64_t timestamp);
    void handle_window_close_event        (int64_t timestamp);
    void handle_window_focus_event        (int64_t timestamp, bool focused);
    void handle_cursor_enter_event        (int64_t timestamp, bool entered);

    void set_input_event_synthesizer_callback(std::function<void(Context_window& context_window)> callback);
    void inject_input_event                  (const Input_event& event);

    [[nodiscard]] auto get_modifier_mask () const -> Key_modifier_mask;
    [[nodiscard]] auto get_device_pointer() const -> void*; // This would be an ID3D11Device, HGLRC/GLXContext, ID3D12Device, etc
    [[nodiscard]] auto get_window_handle () const -> void*; // This would be an HWND, GLXDrawable, etc
    [[nodiscard]] auto get_scale_factor  () const -> float;

#if defined(ERHE_OS_WINDOWS)
    [[nodiscard]] auto get_hwnd() const -> HWND;
    [[nodiscard]] auto get_hglrc() const -> HGLRC;
#endif
#if defined(ERHE_OS_LINUX)
    [[nodiscard]] auto get_wl_display() const -> struct wl_display*;
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    [[nodiscard]] auto get_required_vulkan_instance_extensions() -> const std::vector<std::string>&;
    [[nodiscard]] auto create_vulkan_surface(void* vulkan_instance) -> void*;
#endif

    [[nodiscard]] auto sdl_event_filter(void* event) -> bool;

private:
    struct Joystick_info
    {
        std::vector<float>     axis_values;
        std::vector<bool>      button_values;
    };

    void*                      m_sdl_window                    {nullptr};
    void*                      m_sdl_gl_context                {nullptr};
    Mouse_cursor               m_current_mouse_cursor          {Mouse_cursor_Arrow};
    bool                       m_is_mouse_relative_hold_enabled{false};
    bool                       m_is_window_visible             {false};
    bool                       m_use_raw_mouse                 {false};
    Window_configuration       m_configuration;
    float                      m_last_mouse_x              {0.0f};
    float                      m_last_mouse_y            {0.0f};
    float                      m_mouse_relative_hold_xpos{0.0f};
    float                      m_mouse_relative_hold_ypos{0.0f};
    float                      m_mouse_virtual_xpos      {0.0f};
    float                      m_mouse_virtual_ypos      {0.0f};
    unsigned int               m_key_modifiers{0};
    std::atomic<bool>          m_joystick_scan_done{false};
    std::vector<Joystick_info> m_joystick_info;
    int                        m_input_event_queue_write{0};
    std::vector<Input_event>   m_input_events[2];
    std::thread                m_joystick_scan_task;
    std::function<void(Context_window& context_window)> m_input_event_synthesizer_callback;
    std::function<void()>      m_redraw_callback;

#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    std::vector<std::string> m_required_instance_extensions;
#endif

    static int s_window_count;
};

} // namespace erhe::window
