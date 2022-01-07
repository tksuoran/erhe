#include "command.hpp"
#include "tools/pointer_context.hpp"
#include "editor_view.hpp"
#include "log.hpp"

#include "erhe/toolkit/verify.hpp"

namespace editor {

Command_context::Command_context(
    Editor_view&     editor_view,
    Pointer_context& pointer_context
)
    : m_editor_view    {editor_view}
    , m_pointer_context{pointer_context}
{
}

auto Command_context::viewport_window() -> Viewport_window*
{
    return m_pointer_context.window();
}

auto Command_context::hovering_over_tool() -> bool
{
    return m_pointer_context.hovering_over_tool();
}

auto Command_context::accept_mouse_command(Command* command) -> bool
{
    return m_editor_view.accept_mouse_command(command);
}

Command::Command(const char* name)
    : m_name{name}
{
}

Command::~Command() = default;

auto Command::try_call(Command_context& context) -> bool
{
    static_cast<void>(context);
    return false;
}

void Command::try_ready(Command_context& context)
{
    static_cast<void>(context);
}

void Command::on_inactive(Command_context& context)
{
    static_cast<void>(context);
}

auto Command::state() const -> State
{
    return m_state;
}

void Command::set_inactive(Command_context& context)
{
    log_command_state_transition.trace("{} -> inactive", name());
    on_inactive(context);
    m_state = State::Inactive;
};

void Command::set_ready(Command_context& context)
{
    static_cast<void>(context);
    log_command_state_transition.trace("{} -> ready", name());
    m_state = State::Ready;
}

void Command::set_active(Command_context& context)
{
    static_cast<void>(context);
    log_command_state_transition.trace("{} -> active", name());
    m_state = State::Active;
}

auto Command::name() const -> const char*
{
    return m_name;
}

Command_binding::Command_binding()
{
}

Command_binding::Command_binding(Command* command)
    : m_command{command}
{
}

Command_binding::~Command_binding() = default;

auto Command_binding::get_id() const -> erhe::toolkit::Unique_id<Command_binding>::id_type
{
    return m_id.get_id();
}

auto Command_binding::get_command() const -> Command*
{
    return m_command;
}

Key_binding::Key_binding(
    Command*                      command,
    const erhe::toolkit::Keycode  code,
    const bool                    pressed,
    const std::optional<uint32_t> modifier_mask
)
    : Command_binding{command      }
    , m_code         {code         }
    , m_pressed      {pressed      }
    , m_modifier_mask{modifier_mask}
{
}

//Key_binding::~Key_binding() = default;

auto Key_binding::on_key(
    Command_context&             context,
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
) -> bool
{
    if (
        (m_code    != code   ) ||
        (m_pressed != pressed)
    )
    {
        return false;
    }

    auto* command = get_command();

    if (
        m_modifier_mask.has_value() &&
        m_modifier_mask.value() != modifier_mask)
    {
        log_input_event_filtered.trace(
            "{} rejected key {} due to modifier mask mismatch",
            command->name(),
            pressed ? "press" : "release",
            erhe::toolkit::c_str(code)
        );
        return false;
    }

    if (command->state() == State::Disabled)
    {
        return false;
    }

    const bool consumed = command->try_call(context);
    if (consumed)
    {
        log_input_event_consumed.trace(
            "{} consumed key {} {}",
            command->name(),
            erhe::toolkit::c_str(code),
            pressed ? "press" : "release"
        );
    }
    return consumed && pressed;
}

Mouse_binding::Mouse_binding(Command* command)
    : Command_binding{command}
{
}

//Mouse_binding::~Mouse_binding() = default;

auto Mouse_binding::on_button(
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

auto Mouse_binding::on_motion(Command_context& context) -> bool
{
    static_cast<void>(context);
    return false;
}

Mouse_click_binding::Mouse_click_binding(
    Command*                          command,
    const erhe::toolkit::Mouse_button button
)
    : Mouse_binding{command}
    , m_button     {button }
{
}

//Mouse_click_binding::~Mouse_click_binding() = default;

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
            log_input_event_consumed.trace(
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
    //ERHE_VERIFY(command != nullptr);

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

Mouse_motion_binding::Mouse_motion_binding(Command* command)
    : Mouse_binding{command}
{
}

//Mouse_motion_binding::~Mouse_motion_binding() = default;

auto Mouse_motion_binding::on_motion(Command_context& context) -> bool
{
    auto* command = get_command();

    if (command->state() == State::Disabled)
    {
        return false;
    }

    // Motion binding never consumes the event, so that all
    // motion bindings can process the motion.
    static_cast<void>(command->try_call(context));
    return false;
}

Mouse_drag_binding::Mouse_drag_binding(
    Command*                          command,
    const erhe::toolkit::Mouse_button button
)
    : Mouse_binding{command}
    , m_button     {button }
{
}

//Mouse_drag_binding::~Mouse_drag_binding() = default;

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

    if (command->state() == State::Disabled)
    {
        return false;
    }

    if (!context.accept_mouse_command(command))
    {
        ERHE_VERIFY(command->state() == State::Inactive);
        log_input_event_filtered.trace(
            "{} not active so button event ignored",
            command->name()
        );
        return false;
    }

    // Mouse button down when in Inactive state -> transition to Ready state
    if (count > 0)
    {
        if (command->state() == State::Inactive)
        {
            command->try_ready(context);
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
            log_input_event_consumed.trace(
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

    if (command->state() == State::Ready)
    {
        command->set_active(context);
    }

    const bool consumed = command->try_call(context);;
    if (consumed)
    {
        log_input_event_consumed.trace(
            "{} consumed mouse drag motion",
            command->name()
        );
    }
    return consumed;
}

}  // namespace editor
