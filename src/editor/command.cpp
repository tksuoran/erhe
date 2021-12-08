#include "command.hpp"
#include "tools/pointer_context.hpp"
#include "editor_view.hpp"

#include "windows/log_window.hpp"

#include "erhe/toolkit/verify.hpp"

namespace editor {

auto Command_context::viewport_window() -> Viewport_window*
{
    return m_pointer_context.window();
}

auto Command_context::hovering_over_tool() -> bool
{
    return m_pointer_context.hovering_over_tool();
}

auto Command_context::log_window() -> Log_window*
{
    return m_log_window;
}

auto Command_context::accept_mouse_command(Command* command) -> bool
{
    return m_editor_view.accept_mouse_command(command);
}

auto Key_binding::on_key(
    Command_context&             context,
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
) -> bool
{
    if (
        (m_code          != code         ) ||
        (m_pressed       != pressed      ) ||
        (m_modifier_mask != modifier_mask)
    )
    {
        return false;
    }

    auto* command = get_command();
    ERHE_VERIFY(command != nullptr);

    if (command->state() == State::Disabled)
    {
        return false;
    }

    const bool consumed = command->try_call(context);
    if (consumed)
    {
        context.log_window()->tail_log(
            "{} consumed key {} {}",
            command->name(),
            erhe::toolkit::c_str(code),
            pressed ? "press" : "release"
        );
    }
    return command->try_call(context);
}

auto Mouse_click_binding::on_button(
    Command_context&                  context,
    const erhe::toolkit::Mouse_button button,
    const int                         count
) -> bool
{
    if (m_button != button)
    {
        return false;
    }

    auto* command = get_command();
    ERHE_VERIFY(command != nullptr);

    if (command->state() == State::Disabled)
    {
        return false;
    }

    if (!context.accept_mouse_command(command))
    {
        // Paranoid check
        if (command->state() != State::Inactive)
        {
            command->set_inactive(context);
        }
        return false;
    }

    // Mouse button down
    if (count > 0)
    {
        if (command->state() == State::Inactive)
        {
            command->try_ready(context);
        }
        return false;
    }
    else // count == 0
    {
        bool consumed{false};
        if (command->state() == State::Ready)
        {
            consumed = command->try_call(context);
            context.log_window()->tail_log(
                "{} consumed mouse button {} click",
                command->name(),
                erhe::toolkit::c_str(button)
            );
        }
        command->set_inactive(context);
        return consumed;
    }
}

auto Mouse_click_binding::on_motion(Command_context& context) -> bool
{
    auto* command = get_command();
    ERHE_VERIFY(command != nullptr);

    if (command->state() == State::Disabled)
    {
        return false;
    }

    // Motion when not in Inactive state -> transition to inactive state
    if (command->state() != State::Inactive)
    {
        command->set_inactive(context);
    }

    return false;
}

auto Mouse_motion_binding::on_motion(Command_context& context) -> bool
{
    auto* command = get_command();
    ERHE_VERIFY(get_command() != nullptr);

    if (command->state() == State::Disabled)
    {
        return false;
    }

    // Motion binding never consumes the event, so that all
    // motion bindings can process the motion.
    static_cast<void>(command->try_call(context));
    return false;
}

auto Mouse_drag_binding::on_button(
    Command_context&                  context,
    const erhe::toolkit::Mouse_button button,
    const int                         count
) -> bool
{
    if (m_button != button)
    {
        return false;
    }

    auto* command = get_command();
    ERHE_VERIFY(command != nullptr);

    if (command->state() == State::Disabled)
    {
        return false;
    }

    if (!context.accept_mouse_command(command))
    {
        ERHE_VERIFY(command->state() == State::Inactive);
        return false;
    }

    // Mouse button down when in Inactive state -> transition to Ready state
    if (count > 0)
    {
        if (command->state() == State::Inactive)
        {
            command->try_ready(context);
            //command->set_ready(context);
        }
        return false;
    }
    else
    {
        bool consumed = false;
        if (command->state() != State::Inactive)
        {
            // Drag binding consumes button release event only
            // if command was ini active state.
            consumed = command->state() == State::Active;
            command->set_inactive(context);
            context.log_window()->tail_log(
                "{} consumed mouse drag release {}",
                command->name(),
                erhe::toolkit::c_str(button)
            );
        }
        return consumed;
    }
}

auto Mouse_drag_binding::on_motion(Command_context& context) -> bool
{
    auto* command = get_command();
    ERHE_VERIFY(command != nullptr);

    if (command->state() == State::Ready)
    {
        command->set_active(context);
    }

    const bool consumed = command->try_call(context);;
    if (consumed)
    {
        context.log_window()->tail_log(
            "{} consumed mouse drag motion",
            command->name()
        );
    }
    return consumed;
}

}  // namespace editor
