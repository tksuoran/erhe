#pragma once

#include "erhe/application/rendergraph/rendergraph_node.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

namespace erhe::application
{

class Imgui_windows;
class View;
class Window;


/// <summary>
/// Base class for derived Imgui_viewport classes - where ImGui windows can be hosted.
/// </summary>
/// Current Imgui_viewport classes are Window_imgui_viewport and Rendertarget_imgui_viewport.
/// Every Imgui_window must be hosted in exactly one Imgui_viewport.
/// Each Imgui_viewport maintains a separate ImGui context.
/// Each Imgui_viewport is a Rendergraph_node and as such must implement
/// execute_rendergraph_node() method for rendering ImGui data with
/// Imgui_renderer::render_draw_data().
class Imgui_viewport
    : public Rendergraph_node
{
public:
    Imgui_viewport(
        const std::string_view name,
        bool                   imgui_ini,
        ImFontAtlas*           font_atlas
    );
    virtual ~Imgui_viewport();

    [[nodiscard]] virtual auto begin_imgui_frame() -> bool = 0;
    virtual void end_imgui_frame() = 0;

    [[nodiscard]] virtual auto get_scale_value() const -> float;

    [[nodiscard]] auto name                 () const -> const std::string&;
    [[nodiscard]] auto want_capture_keyboard() const -> bool;
    [[nodiscard]] auto want_capture_mouse   () const -> bool;
    [[nodiscard]] auto has_cursor           () const -> bool;
    [[nodiscard]] auto imgui_context        () const -> ImGuiContext*;

    void menu();

    void update_input_request(bool request_keyboard, bool request_mouse);

    void on_key         (signed int keycode, uint32_t modifier_mask, bool pressed);
    void on_char        (unsigned int codepoint);
    void on_focus       (int focused);
    void on_cursor_enter(int entered);
    void on_mouse_move  (float x, float y);
    void on_mouse_button(uint32_t button, bool pressed);
    void on_mouse_wheel (float x, float y);

    auto get_mouse_position() const -> glm::vec2;

protected:
    std::string m_name;
    std::string m_imgui_ini_path;
    bool        m_show_menu       {false};
    double      m_time            {0.0};
    bool        m_has_cursor      {false};
    bool        m_request_keyboard{false}; // hovered window requests keyboard events
    bool        m_request_mouse   {false}; // hovered winodw requests mouse events

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGuiContext* m_imgui_context{nullptr};
#endif

};

} // namespace erhe::application
