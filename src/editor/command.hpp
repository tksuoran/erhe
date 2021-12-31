#pragma once

#include "state.hpp"
#include "windows/log_window.hpp"

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
    )
        : m_editor_view    {editor_view}
        , m_pointer_context{pointer_context}
        , m_log_window     {log_window}
    {
    }

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
    explicit Command(const char* name)
        : m_name{name}
    {
    }

    virtual [[nodiscard]] auto try_call(Command_context& context) -> bool
    {
        static_cast<void>(context);
        return false;
    }

    virtual [[nodiscard]] void try_ready(Command_context& context)
    {
        static_cast<void>(context);
    }

    virtual [[nodiscard]] void on_inactive(Command_context& context)
    {
        static_cast<void>(context);
    }

    [[nodiscard]] auto state() const -> State
    {
        return m_state;
    }

    void set_inactive(Command_context& context)
    {
        context.log_window()->tail_log("{} -> inactive", name());
        on_inactive(context);
        m_state = State::Inactive;
    };

    void set_ready(Command_context& context)
    {
        context.log_window()->tail_log("{} -> ready", name());
        //static_cast<void>(context);
        m_state = State::Ready;
    }

    void set_active(Command_context& context)
    {
        context.log_window()->tail_log("{} -> active", name());
        //static_cast<void>(context);
        m_state = State::Active;
    }

    auto name() const -> const char*
    {
        return m_name;
    }

private:
    State       m_state{State::Inactive};
    const char* m_name {nullptr};
};

class Command_binding
{
public:
    explicit Command_binding(Command* command)
        : m_command{command}
    {
    }

    auto get_id() const -> erhe::toolkit::Unique_id<Command_binding>::id_type
    {
        return m_id.get_id();
    }

    auto get_command() const -> Command*
    {
        return m_command;
    }

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
    )
        : Command_binding{command      }
        , m_code         {code         }
        , m_pressed      {pressed      }
        , m_modifier_mask{modifier_mask}
    {
    }

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
    explicit Mouse_binding(Command* command)
        : Command_binding{command}
    {
    }

    virtual auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool
    {
        static_cast<void>(context);
        static_cast<void>(button);
        static_cast<void>(count);
        return false;
    }

    virtual auto on_motion(Command_context& context) -> bool
    {
        static_cast<void>(context);
        return false;
    }
};

// Mouse pressed and released while not being moved
class Mouse_click_binding
    : public Mouse_binding
{
public:
    Mouse_click_binding(
        Command*                          command,
        const erhe::toolkit::Mouse_button button
    )
        : Mouse_binding{command}
        , m_button     {button }
    {
    }

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
    Mouse_motion_binding(Command* command)
        : Mouse_binding{command}
    {
    }

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
    )
        : Mouse_binding{command}
        , m_button     {button }
    {
    }

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

