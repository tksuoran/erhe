#pragma once

#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <glm/glm.hpp>
#include <imgui/imgui.h>

#include <cstdint>
#include <functional>
#include <string_view>

namespace erhe::window {
    class Key_event;
    class Char_event;
    class Window_focus_event;
    class Cursor_enter_event;
    class Mouse_move_event;
    class Mouse_button_event;
    class Mouse_wheel_event;
}

namespace erhe::imgui {

class Imgui_renderer;
class View;
class Window;

// Base class for derived Imgui_host classes - where ImGui windows can be hosted.
//
// - Current Imgui_host classes are Window_imgui_host and Rendertarget_imgui_host.
// - Every Imgui_window must be hosted in exactly one Imgui_host.
// - Each Imgui_host maintains a separate ImGui context.
// - Each Imgui_host is a Rendergraph_node and as such must implement
//   execute_rendergraph_node() method for rendering ImGui data with
//   Imgui_renderer::render_draw_data().
class Imgui_host : public erhe::rendergraph::Rendergraph_node, public erhe::window::Input_event_handler
{
public:
    Imgui_host(
        erhe::rendergraph::Rendergraph& rendergraph,
        Imgui_renderer&                 imgui_renderer,
        erhe::utility::Debug_label      debug_label,
        bool                            imgui_ini,
        ImFontAtlas*                    font_atlas
    );
    ~Imgui_host() noexcept override;

    virtual void begin_imgui_frame  () = 0;
    virtual void process_events     (float dt_s, int64_t time_ns) = 0;
    virtual void end_imgui_frame    () = 0;
    virtual void set_text_input_area(int x, int y, int w, int h) = 0;
    virtual void start_text_input   () = 0;
    virtual void stop_text_input    () = 0;

    void set_begin_callback(const std::function<void(Imgui_host& viewport)>& callback);

    [[nodiscard]] virtual auto is_visible     () const -> bool = 0; // TODO XXX FIX
    [[nodiscard]] virtual auto get_scale_value() const -> float;

    [[nodiscard]] auto name                       () const -> const std::string&;
    [[nodiscard]] auto want_capture_keyboard      () const -> bool;
    [[nodiscard]] auto want_capture_mouse         () const -> bool;
    [[nodiscard]] auto get_window_request_keyboard() const -> bool;
    [[nodiscard]] auto get_window_request_mouse   () const -> bool;
    [[nodiscard]] auto has_cursor                 () const -> bool;
    [[nodiscard]] auto imgui_context              () const -> ImGuiContext*;
    [[nodiscard]] auto get_root_dock_id           () const -> ImGuiID;

    void update_input_request(bool request_keyboard, bool request_mouse);
    void request_cursor_relative_hold();

    auto on_window_focus_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_cursor_enter_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_key_event         (const erhe::window::Input_event& input_event) -> bool override;
    auto on_text_event        (const erhe::window::Input_event& input_event) -> bool override;
    auto on_char_event        (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_move_event  (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_button_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_wheel_event (const erhe::window::Input_event& input_event) -> bool override;

    [[nodiscard]] auto get_mouse_position() const -> glm::vec2;
    [[nodiscard]] auto get_imgui_renderer() -> Imgui_renderer&;
    [[nodiscard]] auto get_imgui_context () -> ImGuiContext*;

    // Save / load this host's ImGui layout (window positions, sizes and docking)
    // to / from an explicit .ini path, independent of io.IniFilename. Used to
    // persist a per-scene window layout next to the scene file so that loading a
    // scene restores how its windows were docked when it was saved.
    void save_imgui_ini(const std::string& path);
    void load_imgui_ini(const std::string& path);

    // Path of the persisted layout ini; empty when this host does not persist
    // its layout. Under a read_only erhe::codegen::Config_persistence policy
    // the ini is read but never written.
    [[nodiscard]] auto get_imgui_ini_path() const -> const std::string&;

    // Replace the automatic layout ini path (derived from the debug label) with
    // an explicit one, or disable persistence with an empty path. Must be called
    // before this host's first imgui frame: ImGui loads io.IniFilename on the
    // first NewFrame, so a later change would skip the load (and split saves
    // across two files).
    void set_imgui_ini_path(const std::string& path);

    // Item recording (doc/erhe/imgui.md). A request arms
    // ImGuiContext::TestEngineHookItems for exactly the next NewFrame ..
    // Render of this host; the frame's records stay readable until the next
    // request. Recording is off in every other frame, so an unrequested frame
    // calls no hook at all - get_item_recorder().get_hook_call_count() is the
    // measurement of that.
    void request_item_recording();

    [[nodiscard]] auto get_item_recorder          () -> Imgui_item_recorder&;
    [[nodiscard]] auto get_item_recorder          () const -> const Imgui_item_recorder&;
    [[nodiscard]] auto is_item_recording_requested() const -> bool;

protected:
    // Called by the derived host around its ImGui::NewFrame() ..
    // ImGui::Render() pair.
    void begin_item_recording();
    void end_item_recording  ();

    // Called by the derived host (with this host's ImGui context current)
    // right before ImGui::NewFrame(): reads the layout ini of a read_only
    // host once, which ImGui does not do itself while io.IniFilename is null.
    void load_pending_imgui_ini();

    std::function<void(Imgui_host& viewport)> m_begin_callback;
    std::string     m_imgui_ini_path;
    bool            m_imgui_ini_load_pending      {false};
    bool            m_has_cursor                  {false};
    bool            m_request_keyboard            {false}; // hovered window requests keyboard events
    bool            m_request_mouse               {false}; // hovered winodw requests mouse events
    bool            m_request_cursor_relative_hold{false};
    Imgui_renderer& m_imgui_renderer;
    ImGuiContext*   m_imgui_context{nullptr};
    ImGuiID         m_root_dock_id {0};

    Imgui_item_recorder m_item_recorder;
    bool                m_item_recording_requested{false};
    bool                m_item_recording_active   {false};

private:
    void apply_imgui_ini_path();
};

} // namespace erhe::imgui
