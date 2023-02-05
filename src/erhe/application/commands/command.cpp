#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "erhe/xr/xr_action.hpp"
#endif

#include <fmt/format.h>

namespace erhe::application {

Command_host::~Command_host() = default;

void Command_host::set_description(const std::string_view description)
{
    m_description = description;
}

auto Command_host::get_description() const -> const char*
{
    return m_description.c_str();
}

auto Command_host::get_priority() const -> int
{
    return 0;
}

auto Command_host::is_enabled() const -> bool
{
    return m_enabled;
}

void Command_host::set_enabled(const bool enabled)
{
    m_enabled = enabled;
}

void Command_host::enable()
{
    m_enabled = true;
}

void Command_host::disable()
{
    m_enabled = false;
}


Command::Command(const std::string_view name)
    : m_name{name}
{
}

Command::~Command() noexcept
{
}

void Command::try_ready()
{
    set_ready();
}

auto Command::try_call() -> bool
{
    return false;
}

auto Command::try_call(Input_arguments& input) -> bool
{
    static_cast<void>(input);
    return try_call();
}

void Command::on_inactive()
{
}

auto Command::get_command_state() const -> State
{
    return m_state;
}

auto Command::get_host() const -> Command_host*
{
    return m_host;
}

auto Command::is_enabled() const -> bool
{
    return m_state != State::Disabled;
}

auto Command::get_target_command() -> Command&
{
    return *this;
}

void Command::set_inactive()
{
    if (m_state == State::Inactive)
    {
        return;
    }
    log_command_state_transition->trace(
        "{} -> inactive",
        get_name()
    );
    on_inactive();
    m_state = State::Inactive;
    g_commands->command_inactivated(this);
};

void Command::disable()
{
    if (m_state == State::Disabled)
    {
        return;
    }

    log_command_state_transition->trace("{} -> disabled", get_name());
    if (m_state == State::Active)
    {
        set_inactive();
    }
    m_state = State::Disabled;
};

void Command::enable()
{
    if (m_state != State::Disabled)
    {
        return;
    }

    log_command_state_transition->trace("{} -> enabled", get_name());
    set_active();
};

void Command::set_ready()
{
    if (m_state == State::Ready)
    {
        return;
    }

    log_command_state_transition->trace("{} -> ready", get_name());
    m_state = State::Ready;
}

void Command::set_active()
{
    if (m_state == State::Active)
    {
        return;
    }
    log_command_state_transition->trace("{} -> active", get_name());
    m_state = State::Active;
}

auto Command::get_name() const -> const char*
{
    return m_name.c_str();
}

void Command::set_host(Command_host* host)
{
    m_host = host;
}

auto Command::get_base_priority() const -> int
{
    switch (m_state)
    {
        //using enum State;
        case State::Active:   return 4;
        case State::Ready:    return 3;
        case State::Inactive: return 2;
        case State::Disabled: return 1;
    }
    return 0;
}

auto Command::get_host_priority() const -> int
{
   return (m_host != nullptr)
        ? m_host->get_priority()
        : 0;
 }

auto Command::get_priority() const -> int
{
    return get_base_priority() * 20 + get_host_priority();
}

//

Helper_command::Helper_command(Command& target_command, std::string name)
    : Command         {name}
    , m_target_command{target_command}
{
}

auto Helper_command::get_priority() const -> int
{
    // Helper commands have tiny priority over target command
    return m_target_command.get_priority() + 1;
}

auto Helper_command::is_enabled() const -> bool
{
    return m_target_command.is_enabled();
}

auto Helper_command::get_command_state() const -> State
{
    return m_target_command.get_command_state();
}

auto Helper_command::get_target_command() -> Command&
{
    return m_target_command.get_target_command();
}

//

Drag_enable_command::Drag_enable_command(Command& update_command)
    : Command{
        fmt::format("Drag_enable_command({})", update_command.get_name())
    }
    , m_update_command{update_command}
{
    update_command.set_host(this);
    set_enabled(false);
    set_description(Command::get_name());
}

auto Drag_enable_command::try_call(Input_arguments& input) -> bool
{
    const bool enable = input.button_pressed;
    this->Command_host::set_enabled(enable);
    if (enable) // TODO This assymetry does not look great
    {
        m_update_command.try_ready();
    }
    else
    {
        m_update_command.get_target_command().set_inactive();
    }
    set_inactive(); // TODO Is this needed?

    const auto target_state    = m_update_command.get_target_command().get_command_state();
    const bool is_target_ready = (target_state == State::Ready) || (target_state == State::Active);
    return is_target_ready;
}

//

Drag_enable_float_command::Drag_enable_float_command(
    Command&    update_command,
    const float min_to_enable,
    const float max_to_disable
)
    : Command{
        fmt::format("Drag_enable_float_command({})", update_command.get_name())
    }
    , m_update_command{update_command}
    , m_min_to_enable {min_to_enable}
    , m_max_to_disable{max_to_disable}
{
    ERHE_VERIFY(min_to_enable > max_to_disable);
    update_command.set_host(this);
    set_enabled(false);
    set_description(Command::get_name());
}

auto Drag_enable_float_command::try_call(Input_arguments& input) -> bool
{
    static_cast<void>(input);

    const bool enable  = !Command_host::is_enabled() && (input.float_value >= m_min_to_enable);
    const bool disable =  Command_host::is_enabled() && (input.float_value <= m_max_to_disable);

    if (!enable && !disable)
    {
        return true; // consumed but didn't make a difference (yet)
    }

    ERHE_VERIFY(enable != disable);

    this->Command_host::set_enabled(enable);
    if (enable) // TODO This assymetry does not look great
    {
        m_update_command.try_ready();
    }
    else
    {
        m_update_command.get_target_command().set_inactive();
    }
    set_inactive(); // TODO Is this needed?

    const auto target_state    = m_update_command.get_target_command().get_command_state();
    const bool is_target_ready = (target_state == State::Ready) || (target_state == State::Active);
    return is_target_ready;
}

//

Redirect_command::Redirect_command(Command& target_command)
    : Helper_command{
        target_command,
        fmt::format("Redirect_command({})", target_command.get_name())
    }
{
}

void Redirect_command::try_ready()
{
    m_target_command.try_ready();
}

auto Redirect_command::try_call() -> bool
{
    return m_target_command.try_call();
}

//

Drag_float_command::Drag_float_command(Command& target_command)
    : Helper_command{
        target_command,
        fmt::format("Drag_float_command({})", target_command.get_name())
    }
{
}

auto Drag_float_command::try_call() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    return m_target_command.try_call();
}

//

Drag_vector2f_command::Drag_vector2f_command(Command& target_command)
    : Helper_command{
        target_command,
        fmt::format("Drag_vector2f_command({})", target_command.get_name())
    }
{
}

auto Drag_vector2f_command::try_call() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    return m_target_command.try_call();
}

auto Drag_vector2f_command::try_call(Input_arguments& input) -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    return m_target_command.try_call(input);
}

//

Drag_pose_command::Drag_pose_command(Command& target_command
)
    : Helper_command{
        target_command,
        fmt::format("Drag_pose_command({})", target_command.get_name())
    }
{
}

auto Drag_pose_command::try_call() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    return m_target_command.try_call();
}

//

#if defined(ERHE_XR_LIBRARY_OPENXR)
Xr_float_click_command::Xr_float_click_command(Command& target_command)
    : Helper_command{
        target_command,
        fmt::format("Xr_float_click_command({})", target_command.get_name())
    }
{
}

void Xr_float_click_command::bind(erhe::xr::Xr_action_float* xr_action_for_value)
{
    m_xr_action_for_value = xr_action_for_value;
}

void Xr_float_click_command::try_ready()
{
    m_target_command.try_ready();
}

auto Xr_float_click_command::try_call() -> bool
{
    Input_arguments input{
        .float_value = m_xr_action_for_value->state.currentState
    };
    return m_target_command.try_call(input);
}

//

Xr_vector2f_click_command::Xr_vector2f_click_command(Command& target_command)
    : Helper_command{
        target_command,
        fmt::format("Xr_vector2f_click_command({})", target_command.get_name())
    }
{
}

void Xr_vector2f_click_command::bind(erhe::xr::Xr_action_vector2f* xr_action_for_value)
{
    m_xr_action_for_value = xr_action_for_value;
}

void Xr_vector2f_click_command::try_ready()
{
    m_target_command.try_ready();
}

auto Xr_vector2f_click_command::try_call() -> bool
{
    Input_arguments input
    {
        .vector2 = {
            .absolute_value = glm::vec2{
                m_xr_action_for_value->state.currentState.x,
                m_xr_action_for_value->state.currentState.y
            }
        }
    };
    return m_target_command.try_call(input);
}

//

Xr_pose_click_command::Xr_pose_click_command(Command& target_command)
    : Helper_command{
        target_command,
        fmt::format("Xr_pose_click_command({})", target_command.get_name())
    }
{
}

void Xr_pose_click_command::bind(erhe::xr::Xr_action_pose* xr_action_for_value)
{
    m_xr_action_for_value = xr_action_for_value;
}

void Xr_pose_click_command::try_ready()
{
    m_target_command.try_ready();
}


auto Xr_pose_click_command::try_call() -> bool
{
    Input_arguments input
    {
        .pose = {
            .orientation = m_xr_action_for_value->orientation,
            .position    = m_xr_action_for_value->position
        }
    };
    return m_target_command.try_call(input);
}
#endif

}  // namespace erhe::application
