#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "erhe_xr/xr_action.hpp"
#endif

#include <fmt/format.h>

namespace erhe::commands {

Command_host::~Command_host() noexcept = default;

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

void Command_host::enable_command_host()
{
    m_enabled = true;
}

void Command_host::disable_command_host()
{
    m_enabled = false;
}


Command::Command(Commands& commands, const std::string_view name)
    : m_commands{commands}
    , m_name    {name}
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

auto Command::try_call_with_input(Input_arguments& input) -> bool
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
    if (m_state == State::Inactive) {
        return;
    }
    log_command_state_transition->trace("{} -> inactive", get_name());
    on_inactive();
    m_state = State::Inactive;
    m_commands.command_inactivated(this);
};

void Command::disable_command()
{
    if (m_state == State::Disabled) {
        return;
    }

    log_command_state_transition->trace("{} -> disabled", get_name());
    if (m_state == State::Active) {
        set_inactive();
    }
    m_state = State::Disabled;
};

void Command::enable_command()
{
    if (m_state != State::Disabled) {
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
    if (m_state == State::Active) {
        return;
    }
    log_command_state_transition->trace("{} -> active", get_name());
    m_state = State::Active;
}

auto Command::is_accepted() const -> bool
{
    return m_commands.accept_mouse_command(this);
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
    switch (m_state) {
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
   return (m_host != nullptr) ? m_host->get_priority() : 0;
 }

auto Command::get_priority() const -> int
{
    return get_base_priority() * 20 + get_host_priority();
}

//

Helper_command::Helper_command(Commands& commands, Command& target_command, const std::string& name)
    : Command         {commands, name}
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

Drag_enable_command::Drag_enable_command(Commands& commands, Command& update_command)
    : Command{commands, fmt::format("Drag_enable_command({})", update_command.get_name())}
    , m_update_command{update_command}
{
    update_command.set_host(this);
    set_enabled(false);
    set_description(Command::get_name());
}

auto Drag_enable_command::try_call_with_input(Input_arguments& input) -> bool
{
    const bool enable = input.variant.button_pressed;
    this->Command_host::set_enabled(enable);
    if (enable) { // TODO This assymetry does not look great
        m_update_command.try_ready();
    } else {
        m_update_command.get_target_command().set_inactive();
    }
    set_inactive(); // TODO Is this needed?

    const auto target_state    = m_update_command.get_target_command().get_command_state();
    const bool is_target_ready = (target_state == State::Ready) || (target_state == State::Active);
    return is_target_ready;
}

//

Drag_enable_float_command::Drag_enable_float_command(
    Commands&   commands,
    Command&    update_command,
    const float min_to_enable,
    const float max_to_disable
)
    : Command         {commands, fmt::format("Drag_enable_float_command({})", update_command.get_name())}
    , m_update_command{update_command}
    , m_min_to_enable {min_to_enable}
    , m_max_to_disable{max_to_disable}
{
    ERHE_VERIFY(min_to_enable > max_to_disable);
    update_command.set_host(this);
    set_enabled(false);
    set_description(Command::get_name());
}

auto Drag_enable_float_command::try_call_with_input(Input_arguments& input) -> bool
{
    const bool enable  = !Command_host::is_enabled() && (input.variant.float_value >= m_min_to_enable);
    const bool disable =  Command_host::is_enabled() && (input.variant.float_value <= m_max_to_disable);

    if (!enable && !disable) {
        return true; // consumed but didn't make a difference (yet)
    }

    ERHE_VERIFY(enable != disable);

    this->Command_host::set_enabled(enable);
    if (enable) { // TODO This assymetry does not look great
        m_update_command.try_ready();
    } else {
        m_update_command.get_target_command().set_inactive();
    }
    set_inactive(); // TODO Is this needed?

    const auto target_state    = m_update_command.get_target_command().get_command_state();
    const bool is_target_ready = (target_state == State::Ready) || (target_state == State::Active);
    return is_target_ready;
}

Float_threshold_command::Float_threshold_command(Commands& commands, Command& target_command, float min_to_activate, float max_to_deactivate)
    : Command            {commands, fmt::format("Float_threshold_command({})", target_command.get_name())}
    , m_target_command   {target_command}
    , m_min_to_activate  {min_to_activate}
    , m_max_to_deactivate{max_to_deactivate}
{
    ERHE_VERIFY(min_to_activate > max_to_deactivate);
    set_description(Command::get_name());
}

auto Float_threshold_command::try_call_with_input(Input_arguments& input) -> bool
{
    if (!m_is_active) {
        if (input.variant.float_value >= m_min_to_activate) {
            m_target_command.try_call();
            m_is_active = true;
        }
    } else {
        if (input.variant.float_value <= m_max_to_deactivate) {
            m_is_active = false;
        }
    }
    return false; // TODO
}

//

Redirect_command::Redirect_command(Commands& commands, Command& target_command)
    : Helper_command{commands, target_command, fmt::format("Redirect_command({})", target_command.get_name())}
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

Drag_float_command::Drag_float_command(Commands& commands, Command& target_command)
    : Helper_command{commands, target_command, fmt::format("Drag_float_command({})", target_command.get_name())}
{
}

auto Drag_float_command::try_call() -> bool
{
    if (!is_enabled()) {
        return false;
    }
    return m_target_command.try_call();
}

//

Drag_vector2f_command::Drag_vector2f_command(Commands& commands, Command& target_command)
    : Helper_command{commands, target_command, fmt::format("Drag_vector2f_command({})", target_command.get_name())}
{
}

auto Drag_vector2f_command::try_call() -> bool
{
    if (!is_enabled()) {
        return false;
    }
    return m_target_command.try_call();
}

auto Drag_vector2f_command::try_call_with_input(Input_arguments& input) -> bool
{
    if (!is_enabled()) {
        return false;
    }
    return m_target_command.try_call_with_input(input);
}

//

Drag_pose_command::Drag_pose_command(Commands& commands, Command& target_command)
    : Helper_command{commands, target_command, fmt::format("Drag_pose_command({})", target_command.get_name())}
{
}

auto Drag_pose_command::try_call() -> bool
{
    if (!is_enabled()) {
        return false;
    }
    return m_target_command.try_call();
}

Lambda_command::Lambda_command(Commands& commands, const std::string_view name, std::function<bool()> callback)
    : Command{commands, name}
    , m_callback{callback}
{
}

auto Lambda_command::try_call() -> bool
{
    return m_callback();
}

//

#if defined(ERHE_XR_LIBRARY_OPENXR)
Xr_float_click_command::Xr_float_click_command(Commands& commands, Command& target_command)
    : Helper_command{commands, target_command, fmt::format("Xr_float_click_command({})", target_command.get_name())}
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
        .variant = {
            .float_value = m_xr_action_for_value->state.currentState
        }
    };
    return m_target_command.try_call_with_input(input);
}

//

Xr_vector2f_click_command::Xr_vector2f_click_command(Commands& commands, Command& target_command)
    : Helper_command{commands, target_command, fmt::format("Xr_vector2f_click_command({})", target_command.get_name())}
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
    Input_arguments input{
        .variant = {
            .vector2 = {
                .absolute_value = glm::vec2{
                    m_xr_action_for_value->state.currentState.x,
                    m_xr_action_for_value->state.currentState.y
                }
            }
        }
    };
    return m_target_command.try_call_with_input(input);
}

//

Xr_pose_click_command::Xr_pose_click_command(Commands& commands, Command& target_command)
    : Helper_command{commands, target_command, fmt::format("Xr_pose_click_command({})", target_command.get_name())}
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
    Input_arguments input{
        .variant = {
            .pose = {
                .orientation = m_xr_action_for_value->orientation,
                .position    = m_xr_action_for_value->position
            }
        }
    };
    return m_target_command.try_call_with_input(input);
}
#endif

}  // namespace erhe::commands
