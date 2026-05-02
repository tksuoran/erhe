#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace erhe::graphics { class Command_buffer; class Device; }
namespace erhe::renderer { class Text_renderer; }
namespace erhe::window   { class Context_window; }
namespace erhe::xr       { class Headset; }

namespace editor {

class Init_status_display
{
public:
    // headset (optional): when non-null, render_present() drives an
    // OpenXR frame (xrPollEvent / xrWaitFrame / xrBeginFrame /
    // multiview render pass with text overlay / xrEndFrame) so the
    // loading screen is visible inside the headset during init. When
    // null, the existing desktop swapchain path is used. Pure-headset
    // builds without a desktop window can pass headset != nullptr and
    // skip the swapchain entirely.
    Init_status_display(
        erhe::graphics::Device&          graphics_device,
        erhe::graphics::Command_buffer*& init_command_buffer,
        erhe::window::Context_window&    window,
        erhe::renderer::Text_renderer&   text_renderer,
        bool                             enabled,
        erhe::xr::Headset*               headset = nullptr
    );
    ~Init_status_display() noexcept;

    Init_status_display (const Init_status_display&) = delete;
    void operator=      (const Init_status_display&) = delete;
    Init_status_display (Init_status_display&&)      = delete;
    void operator=      (Init_status_display&&)      = delete;

    void set_line(std::size_t line_index, std::string_view text);
    void render_present();

private:
    static constexpr std::chrono::milliseconds kMinPresentInterval{16};

    void render_present_desktop();
    void render_present_xr     ();

    erhe::graphics::Device&           m_graphics_device;
    // Reference to the editor's current init Command_buffer pointer. The
    // editor opens an init cb before constructing Init_status_display;
    // render_present() ends + submits it, drives a swapchain or XR
    // frame for the loading screen, then opens a fresh init cb and
    // reseats the pointer so subsequent constructor code sees the new
    // cb.
    erhe::graphics::Command_buffer*&  m_init_command_buffer;
    erhe::window::Context_window&     m_window;
    erhe::renderer::Text_renderer&    m_text_renderer;
    erhe::xr::Headset*                m_headset{nullptr};
    bool                              m_enabled{false};
    std::vector<std::string>          m_lines;
};

} // namespace editor
