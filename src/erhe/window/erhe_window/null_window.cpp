#include "erhe_window/null_window.hpp"
#include "erhe_window/window_log.hpp"

namespace erhe::window {

Context_window::Context_window(const Window_configuration& configuration)
    : m_configuration{configuration}
{
    log_window->info("Null window backend: created window '{}' ({}x{})", configuration.title, configuration.size.x, configuration.size.y);
}

Context_window::~Context_window() noexcept = default;

auto Context_window::get_window_configuration() const -> const Window_configuration&
{
    return m_configuration;
}

auto Context_window::get_width() const -> int
{
    return m_configuration.size.x;
}

auto Context_window::get_height() const -> int
{
    return m_configuration.size.y;
}

auto Context_window::get_cursor_relative_hold() const -> bool
{
    return false;
}

auto Context_window::get_input_events() -> std::vector<Input_event>&
{
    const int read_index = 1 - m_input_event_queue_write;
    return m_input_events[read_index];
}

void Context_window::register_redraw_callback(std::function<void()> callback)
{
    m_redraw_callback = std::move(callback);
}

auto Context_window::open(const Window_configuration& configuration) -> bool
{
    m_configuration = configuration;
    return true;
}

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
void Context_window::make_current() const
{
}

void Context_window::clear_current() const
{
}

auto Context_window::delay_before_swap(float seconds) const -> bool
{
    static_cast<void>(seconds);
    return false;
}

void Context_window::swap_buffers() const
{
}
#endif

void Context_window::poll_events(float wait_time)
{
    static_cast<void>(wait_time);

    // Swap event buffers
    const int write_index = m_input_event_queue_write;
    m_input_event_queue_write = 1 - write_index;
    m_input_events[m_input_event_queue_write].clear();

    if (m_input_event_synthesizer_callback) {
        m_input_event_synthesizer_callback(*this);
    }
}

void Context_window::get_cursor_position(float& xpos, float& ypos)
{
    xpos = 0.0f;
    ypos = 0.0f;
}

void Context_window::get_cursor_relative_hold_position(float& xpos, float& ypos)
{
    xpos = 0.0f;
    ypos = 0.0f;
}

void Context_window::set_visible(bool visible)
{
    static_cast<void>(visible);
}

void Context_window::set_cursor(Mouse_cursor cursor)
{
    static_cast<void>(cursor);
}

void Context_window::set_cursor_relative_hold(bool relative_hold_enabled)
{
    static_cast<void>(relative_hold_enabled);
}

void Context_window::set_text_input_area(int x, int y, int w, int h)
{
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(w);
    static_cast<void>(h);
}

void Context_window::start_text_input()
{
}

void Context_window::stop_text_input()
{
}

void Context_window::set_input_event_synthesizer_callback(std::function<void(Context_window& context_window)> callback)
{
    m_input_event_synthesizer_callback = std::move(callback);
}

void Context_window::inject_input_event(const Input_event& event)
{
    m_input_events[m_input_event_queue_write].push_back(event);
}

auto Context_window::get_modifier_mask() const -> Key_modifier_mask
{
    return 0;
}

auto Context_window::get_device_pointer() const -> void*
{
    return nullptr;
}

auto Context_window::get_window_handle() const -> void*
{
    return nullptr;
}

auto Context_window::get_scale_factor() const -> float
{
    return 1.0f;
}

#if defined(ERHE_OS_WINDOWS)
auto Context_window::get_hwnd() const -> HWND
{
    return nullptr;
}

auto Context_window::get_hglrc() const -> HGLRC
{
    return nullptr;
}
#endif

#if defined(ERHE_OS_LINUX)
auto Context_window::get_wl_display() const -> struct wl_display*
{
    return nullptr;
}
#endif

#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
auto Context_window::get_required_vulkan_instance_extensions() -> const std::vector<std::string>&
{
    return m_required_instance_extensions;
}

auto Context_window::create_vulkan_surface(void* vulkan_instance) -> void*
{
    static_cast<void>(vulkan_instance);
    return nullptr;
}
#endif

} // namespace erhe::window
