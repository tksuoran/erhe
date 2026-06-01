#pragma once

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace erhe::graphics { class Command_buffer; class Device; class Texture; }
namespace erhe::renderer { class Text_renderer; }
namespace erhe::window   { class Context_window; }
namespace erhe::xr       { class Headset; class Quad_layer; }

namespace editor {

class Init_status_display
{
public:
    // headset (optional): when non-null, pump() drives an OpenXR frame
    // (xrPollEvent / xrWaitFrame / xrBeginFrame / multiview render
    // pass with text overlay / xrEndFrame) so the loading screen is
    // visible inside the headset during init. When null, the existing
    // desktop swapchain path is used. Pure-headset builds without a
    // desktop window can pass headset != nullptr and skip the
    // swapchain entirely.
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

    // Thread-safe: stores the line text under the state mutex and
    // sets a dirty flag. Safe to call from taskflow workers.
    void set_line       (std::size_t line_index, std::string_view text);

    // Main-thread only: drains pending line state and, if dirty,
    // polls window events and drives a swapchain or XR frame.
    // Drives SDL_PollEvent and xrBegin/EndFrame which must not be
    // called from worker threads.
    void pump();

private:
    static constexpr std::chrono::milliseconds kMinPresentInterval{16};

    void render_present_desktop();
    void render_present_xr     ();

    // Records a render pass that clears `color_texture` (and optionally
    // `depth_stencil_texture`) to m_clear_color and, when `draw_text` is
    // true, draws the current m_lines centered on top. Shared by the
    // multiview and per-eye XR paths. `view_mask` is 0 for single-layer
    // (per-eye, desktop) and a layer bitmask for the true multiview path.
    // All XR callers bind the layer-0 texture, so the previous
    // `texture_layer` parameter was always 0; dropped. `draw_text = false`
    // performs the clear only -- used to clear the XR projection background
    // (transparent) when the status text lives on the world-placed quad.
    void render_text_overlay(
        erhe::graphics::Command_buffer& command_buffer,
        erhe::graphics::Texture*        color_texture,
        erhe::graphics::Texture*        depth_stencil_texture,
        int                             width,
        int                             height,
        unsigned int                    view_mask,
        const char*                     debug_label,
        bool                            draw_text,
        const std::array<double, 4>&    clear_color
    );

#if defined(ERHE_XR_LIBRARY_OPENXR)
    // Create the world-placed loading-screen quad layer once the XR session
    // exists. Idempotent; on failure m_quad_layer stays null and the XR path
    // falls back to head-locked screen-space text.
    void xr_ensure_quad_layer();

    // Capture the initial head pose once and place the quad world-locked in
    // front of the user. Returns true once the pose has been captured (the
    // quad is then visible and submittable). Returns false while no valid
    // head pose is available yet.
    auto xr_try_capture_initial_pose() -> bool;

    // Acquire the quad swapchain image, render the status text into it, and
    // release it (so Xr_session::end_frame() composites it this frame).
    void xr_render_text_into_quad(erhe::graphics::Command_buffer& command_buffer);
#endif

#if defined(ERHE_XR_LIBRARY_OPENXR)
    // World-placed loading-screen quad. Created lazily once the XR session
    // exists; placed once from the initial head pose and then world-locked.
    // Destroyed with this object at the end of editor init (unregisters from
    // the session), before the real Hud / Hotbar quads are created.
    static constexpr uint32_t kQuadWidthPx          {1024}; // quad swapchain pixels (2:1)
    static constexpr uint32_t kQuadHeightPx         {512};
    static constexpr float    kQuadDistanceM        {0.7f}; // meters in front of the head
    static constexpr float    kQuadWidthM           {1.2f}; // physical size (2:1)
    static constexpr float    kQuadHeightM          {0.6f};
    static constexpr int      kQuadCompositionOrder {-100}; // behind later Hud / Hotbar quads

    std::unique_ptr<erhe::xr::Quad_layer> m_quad_layer;
    bool                                  m_quad_creation_attempted{false};
    bool                                  m_quad_pose_captured     {false};
    glm::mat4                             m_quad_world_from_quad   {1.0f};
#endif

    erhe::graphics::Device&           m_graphics_device;
    // Reference to the editor's current init Command_buffer pointer. The
    // editor opens an init cb before constructing Init_status_display;
    // pump() ends + submits it, drives a swapchain or XR frame for the
    // loading screen, then opens a fresh init cb and reseats the
    // pointer so subsequent constructor code sees the new cb.
    erhe::graphics::Command_buffer*&  m_init_command_buffer;
    erhe::window::Context_window&     m_window;
    erhe::renderer::Text_renderer&    m_text_renderer;
    erhe::xr::Headset*                m_headset{nullptr};
    bool                              m_enabled{false};

    // Shared state written by worker threads via set_line /
    // set_clear_color, drained by pump() on the main thread.
    std::mutex                        m_state_mutex;
    std::vector<std::string>          m_lines;         // protected by m_state_mutex
    std::array<double, 4>             m_clear_color{0.01, 0.02, 0.06, 1.0}; // very dark blue; protected by m_state_mutex
    bool                              m_dirty{false};  // protected by m_state_mutex

    // Snapshot consumed by render_text_overlay on the main thread. Not
    // shared with workers; populated by pump() under m_state_mutex
    // then read without locking from the rendering path.
    std::vector<std::string>          m_render_lines;
    std::array<double, 4>             m_render_clear_color{0.01, 0.02, 0.06, 1.0};
};

} // namespace editor
