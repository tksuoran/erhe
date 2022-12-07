#include "erhe/application/commands/controller_trigger_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Controller_trigger_binding::Controller_trigger_binding(Command* const command)
    : Command_binding{command}
{
}

Controller_trigger_binding::Controller_trigger_binding()
{
}

Controller_trigger_binding::~Controller_trigger_binding() noexcept
{
}

Controller_trigger_binding::Controller_trigger_binding(
    Controller_trigger_binding&& other
) noexcept = default;

auto Controller_trigger_binding::operator=(
    Controller_trigger_binding&& other
) noexcept -> Controller_trigger_binding& = default;

[[nodiscard]] auto Controller_trigger_binding::get_type() const -> Type
{
    return Type::Controller_trigger_click;
}

auto Controller_trigger_binding::on_trigger_value(
    Command_context& context
) -> bool
{
    static_cast<void>(context);
    return false;
}

auto Controller_trigger_binding::on_trigger_click(
    Command_context& context,
    const bool       click
) -> bool
{
    static_cast<void>(context);
    static_cast<void>(click);
    return false;
}

auto Controller_trigger_binding::on_trigger_update(
    Command_context& context
) -> bool
{
    static_cast<void>(context);
    return false;
}

} // namespace erhe::application

