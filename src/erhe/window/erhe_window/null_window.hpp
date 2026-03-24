#pragma once

#include "erhe_window/window_event_handler.hpp"
#include "erhe_window/window_configuration.hpp"

#include <functional>
#include <string>
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
constexpr Mouse_cursor Mouse_cursor_TextInput  =  1;
constexpr Mouse_cursor Mouse_cursor_ResizeAll  =  2;
constexpr Mouse_cursor Mouse_cursor_ResizeNS   =  3;
constexpr Mouse_cursor Mouse_cursor_ResizeEW   =  4;
constexpr Mouse_cursor Mouse_cursor_ResizeNESW =  5;
constexpr Mouse_cursor Mouse_cursor_ResizeNWSE =  6;
constexpr Mouse_cursor Mouse_cursor_Hand       =  7;
constexpr Mouse_cursor Mouse_cursor_NotAllowed =  8;
constexpr Mouse_cursor Mouse_cursor_Crosshair  =  9;
constexpr Mouse_cursor Mouse_cursor_COUNT      = 10;

class Context_window
{
public:
    explicit Context_window(const Window_configuration& configuration);
    virtual ~Context_window() noexcept;

    [[nodiscard]] auto get_window_configuration() const -> const Window_configuration&;
    [[nodiscard]] auto get_width               () const -> int;
    [[nodiscard]] auto get_height              () const -> int;
    [[nodiscard]] auto get_cursor_relative_hold() const -> bool;
    [[nodiscard]] auto get_input_events        () -> std::vector<Input_event>&;

    void register_redraw_callback(std::function<void()> callback);

    auto open                             (const Window_configuration& configuration) -> bool;

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    void make_current                     () const;
    void clear_current                    () const;
    auto delay_before_swap                (float seconds) const -> bool;
    void swap_buffers                     () const;
#endif
    void poll_events                      (float wait_time = 0.0f);
    void get_cursor_position              (float& xpos, float& ypos);
    void get_cursor_relative_hold_position(float& xpos, float& ypos);
    void set_visible                      (bool visible);
    void set_cursor                       (Mouse_cursor cursor);
    void set_cursor_relative_hold         (bool relative_hold_enabled);
    void set_text_input_area              (int x, int y, int w, int h);
    void start_text_input                 ();
    void stop_text_input                  ();

    void set_input_event_synthesizer_callback(std::function<void(Context_window& context_window)> callback);
    void inject_input_event                  (const Input_event& event);

    [[nodiscard]] auto get_modifier_mask () const -> Key_modifier_mask;
    [[nodiscard]] auto get_device_pointer() const -> void*;
    [[nodiscard]] auto get_window_handle () const -> void*;
    [[nodiscard]] auto get_scale_factor  () const -> float;

#if defined(ERHE_OS_WINDOWS)
    [[nodiscard]] auto get_hwnd () const -> HWND;
    [[nodiscard]] auto get_hglrc() const -> HGLRC;
#endif
#if defined(ERHE_OS_LINUX)
    [[nodiscard]] auto get_wl_display() const -> struct wl_display*;
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    [[nodiscard]] auto get_required_vulkan_instance_extensions() -> const std::vector<std::string>&;
    [[nodiscard]] auto create_vulkan_surface(void* vulkan_instance) -> void*;
#endif

private:
    Window_configuration     m_configuration;
    int                      m_input_event_queue_write{0};
    std::vector<Input_event> m_input_events[2];
    std::function<void(Context_window& context_window)> m_input_event_synthesizer_callback;
    std::function<void()>    m_redraw_callback;

#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    std::vector<std::string> m_required_instance_extensions;
#endif
};

} // namespace erhe::window
