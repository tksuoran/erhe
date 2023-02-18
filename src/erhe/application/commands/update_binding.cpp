#include "erhe/application/commands/update_binding.hpp"
#include "erhe/application/commands/command.hpp"

namespace erhe::application {

Update_binding::Update_binding(Command* const command)
    : Command_binding{command}
{
}

Update_binding::Update_binding() = default;

Update_binding::~Update_binding() noexcept = default;

auto Update_binding::on_update() -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled) {
        return false;
    }
    auto* host = command->get_host();
    if (
        (host != nullptr) &&
        !host->is_enabled()
    ) {
        return false;
    }
    const bool consumed = command->try_call();
    static_cast<void>(consumed);
    return false;
}

} // namespace erhe::application

