#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace erhe::graphics { class Command_buffer; class Device; class Texture; }
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

    void set_line       (std::size_t line_index, std::string_view text);
    void set_clear_color(std::array<double, 4> color);
    void render_present ();

private:
    static constexpr std::chrono::milliseconds kMinPresentInterval{16};

    void render_present_desktop();
    void render_present_xr     ();

    // Records a render pass that clears `color_texture` (and optionally
    // `depth_stencil_texture`) to m_clear_color and draws the current
    // m_lines centered on top. Shared by the multiview, per-layer
    // multiview-swapchain, and per-eye XR paths. `view_mask` is 0 for
    // single-layer (per-eye, per-layer of a multiview swapchain, and
    // desktop analogues) and a layer bitmask for the true multiview
    // path. `texture_layer` selects an array layer of `color_texture`
    // (and `depth_stencil_texture`) when view_mask == 0; ignored on
    // the multiview path.
    void render_text_overlay(
        erhe::graphics::Command_buffer& command_buffer,
        erhe::graphics::Texture*        color_texture,
        erhe::graphics::Texture*        depth_stencil_texture,
        int                             width,
        int                             height,
        unsigned int                    view_mask,
        unsigned int                    texture_layer,
        const char*                     debug_label
    );

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
    std::array<double, 4>             m_clear_color{0.01, 0.02, 0.06, 1.0}; // very dark blue
};

} // namespace editor
