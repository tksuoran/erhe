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

auto Update_binding::on_update(Input_arguments& input) -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }
    auto* host = command->get_host();
    if (
        (host != nullptr) &&
        !host->is_enabled()
    )
    {
        return false;
    }
    const bool consumed = command->try_call(input);
    static_cast<void>(consumed);
    return false;
}

} // namespace erhe::application

