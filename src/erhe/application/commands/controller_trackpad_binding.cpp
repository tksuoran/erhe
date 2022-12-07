#include "erhe/application/commands/controller_trackpad_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Controller_trackpad_binding::Controller_trackpad_binding(
    Command* const              command,
    const Command_binding::Type type
)
    : Command_binding{command}
    , m_type         {type}
{
    ERHE_VERIFY(
        (type == Type::Controller_trackpad_touched) ||
        (type == Type::Controller_trackpad_clicked)
    );
}

Controller_trackpad_binding::Controller_trackpad_binding()
{
}

Controller_trackpad_binding::~Controller_trackpad_binding() noexcept
{
}

[[nodiscard]] auto Controller_trackpad_binding::get_type() const -> Type
{
    return m_type;
}

auto Controller_trackpad_binding::on_trackpad(
    Command_context& context
) -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    command->try_ready(context);
    const bool consumed = command->try_call(context);
    if (consumed)
    {
        log_input_event_consumed->info(
            "{} consumed controller trackpad",
            command->get_name()
        );
    }

    return consumed;
}

} // namespace erhe::application

