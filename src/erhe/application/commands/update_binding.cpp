#include "erhe/application/commands/update_binding.hpp"
#include "erhe/application/commands/command.hpp"

namespace erhe::application {

Update_binding::Update_binding(Command* const command)
    : Command_binding{command}
{
}

Update_binding::Update_binding()
{
}

Update_binding::~Update_binding() noexcept
{
}

Update_binding::Update_binding(Update_binding&& other) noexcept
    : Command_binding{std::move(other)}
{
}

auto Update_binding::operator=(Update_binding&& other) noexcept -> Update_binding&
{
    Command_binding::operator=(std::move(other));
    return *this;
}

auto Update_binding::on_update(Command_context& context) -> bool
{
    static_cast<void>(context);
    auto* const command = get_command();
    if (command->get_tool_state() == State::Disabled)
    {
        return false;
    }
    const bool consumed = command->try_call(context);
    static_cast<void>(consumed);
    return false;
}

} // namespace erhe::application

