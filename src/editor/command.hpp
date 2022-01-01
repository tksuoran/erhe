#pragma once

#include "state.hpp"

#include "erhe/toolkit/unique_id.hpp"
#include "erhe/toolkit/view.hpp"

#include <functional>

namespace editor {

class Command;
class Editor_view;
class Log_window;
class Pointer_context;
class Viewport_window;

class Command_context
{
public:
    Command_context(
        Editor_view&     editor_view,
        Pointer_context& pointer_context,
        Log_window*      log_window
    );

    [[nodiscard]] auto viewport_window     () -> Viewport_window*;
    [[nodiscard]] auto hovering_over_tool  () -> bool;
    [[nodiscard]] auto log_window          () -> Log_window*;
    [[nodiscard]] auto accept_mouse_command(Command* command) -> bool;

private:
    Editor_view&     m_editor_view;
    Log_window*      m_log_window;
    Pointer_context& m_pointer_context;
};

class Command
{
public:
    explicit Command(const char* name);

    // Virtual interface
    virtual [[nodiscard]] auto try_call   (Command_context& context) -> bool;
    virtual [[nodiscard]] void try_ready  (Command_context& context);
    virtual [[nodiscard]] void on_inactive(Command_context& context);

    // Non-virtual public API
    [[nodiscard]] auto state() const -> State;
    [[nodiscard]] auto name () const -> const char*;
    void set_inactive(Command_context& context);
    void set_ready   (Command_context& context);
    void set_active  (Command_context& context);

private:
    State       m_state{State::Inactive};
    const char* m_name {nullptr};
};

class Command_binding
{
public:
    explicit Command_binding(Command* command);

    [[nodiscard]] auto get_id     () const -> erhe::toolkit::Unique_id<Command_binding>::id_type;
    [[nodiscard]] auto get_command() const -> Command*;

private:
    Command*                                  m_command{nullptr};
    erhe::toolkit::Unique_id<Command_binding> m_id;
};

// Key pressesed or released
class Key_binding
    : public Command_binding
{
public:
    Key_binding(
        Command*                     command,
        const erhe::toolkit::Keycode code,
        const bool                   pressed,
        const uint32_t               modifier_mask
    );

    auto on_key(
        Command_context&             context,
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    ) -> bool;

private:
    erhe::toolkit::Keycode m_code         {erhe::toolkit::Keycode::Key_unknown};
    bool                   m_pressed      {true};
    uint32_t               m_modifier_mask{0};
};

class Mouse_binding
    : public Command_binding
{
public:
    explicit Mouse_binding(Command* command);

    virtual auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool;

    virtual auto on_motion(Command_context& context) -> bool;
};

// Mouse pressed and released while not being moved
class Mouse_click_binding
    : public Mouse_binding
{
public:
    Mouse_click_binding(
        Command*                          command,
        const erhe::toolkit::Mouse_button button
    );

    auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool override;

    auto on_motion(Command_context& context) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
};

// Mouse moved
class Mouse_motion_binding
    : public Mouse_binding
{
public:
    explicit Mouse_motion_binding(Command* command);

    auto on_motion(Command_context& context) -> bool override;
};

// Mouse button pressed and then moved while pressed
class Mouse_drag_binding
    : public Mouse_binding
{
public:
    Mouse_drag_binding(
        Command*                          command,
        const erhe::toolkit::Mouse_button button
    );

    auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool override;

    auto on_motion(Command_context& context) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
};

} // namespace Editor

