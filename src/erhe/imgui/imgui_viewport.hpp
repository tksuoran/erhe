#pragma once

#include "erhe/rendergraph/rendergraph_node.hpp"

#include <glm/glm.hpp>
#include <imgui/imgui.h>

#include <cstdint>
#include <string_view>

namespace erhe::imgui
{

class Imgui_windows;
class View;
class Window;

class Imgui_builtin_windows
{
public:
    bool demo        {false};
    bool style_editor{false};
    bool metrics     {false};
    bool stack_tool  {false};
};

enum class Imgui_event_type : unsigned int {
    no_event           = 0,
    key_event          = 1,
    char_event         = 2,
    focus_event        = 3,
    cursor_enter_event = 4,
    mouse_move_event   = 5,
    mouse_button_event = 6,
    mouse_wheel_event  = 7
};

class Key_event
{
public:
    signed int keycode;
    uint32_t   modifier_mask;
    bool       pressed;
};

class Char_event
{
public:
    unsigned int codepoint;
};

class Focus_event
{
public:
    int focused;
};

class Cursor_enter_event
{
public:
    int entered;
};

class Mouse_move_event
{
public:
    float x;
    float y;
};

class Mouse_button_event
{
public:
    uint32_t button;
    bool     pressed;
};

class Mouse_wheel_event
{
public:
    float x;
    float y;
};

class Imgui_event
{
public:
    Imgui_event_type type;
    union Imgui_event_union
    {
        Key_event          key_event;
        Char_event         char_event;
        Focus_event        focus_event;
        Cursor_enter_event cursor_enter_event;
        Mouse_move_event   mouse_move_event;
        Mouse_button_event mouse_button_event;
        Mouse_wheel_event  mouse_wheel_event;
    } u;
};

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
    : public erhe::rendergraph::Rendergraph_node
{
public:
    Imgui_viewport(
        erhe::rendergraph::Rendergraph& rendergraph,
        Imgui_windows&                  imgui_windows,
        const std::string_view          name,
        bool                            imgui_ini,
        ImFontAtlas*                    font_atlas
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

    void builtin_imgui_window_menu();
    void menu                     ();

    void update_input_request(bool request_keyboard, bool request_mouse);

    void on_key         (signed int keycode, uint32_t modifier_mask, bool pressed);
    void on_char        (unsigned int codepoint);
    void on_focus       (int focused);
    void on_cursor_enter(int entered);
    void on_mouse_move  (float x, float y);
    void on_mouse_button(uint32_t button, bool pressed);
    void on_mouse_wheel (float x, float y);

    void on_event(const Key_event&          key_event);
    void on_event(const Char_event&         char_event);
    void on_event(const Focus_event&        focus_event);
    void on_event(const Cursor_enter_event& cursor_enter_event);
    void on_event(const Mouse_move_event&   mouse_move_event);
    void on_event(const Mouse_button_event& mouse_button_event);
    void on_event(const Mouse_wheel_event&  mouse_wheel_event);

    [[nodiscard]] auto get_mouse_position() const -> glm::vec2;
    [[nodiscard]] auto get_imgui_windows () -> Imgui_windows&;

protected:
    void flush_queud_events();

    std::string              m_imgui_ini_path;
    bool                     m_show_menu       {false};
    double                   m_time            {0.0};
    bool                     m_has_cursor      {false};
    bool                     m_request_keyboard{false}; // hovered window requests keyboard events
    bool                     m_request_mouse   {false}; // hovered winodw requests mouse events
    Imgui_builtin_windows    m_imgui_builtin_windows;

    std::mutex               m_event_mutex;
    std::vector<Imgui_event> m_events;

    Imgui_windows& m_imgui_windows;
    ImGuiContext*  m_imgui_context{nullptr};
};

} // namespace erhe::imgui
