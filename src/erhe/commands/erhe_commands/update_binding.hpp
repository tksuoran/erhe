#pragma once

#include "erhe_commands/command_binding.hpp"

#include "erhe_window/window_event_handler.hpp"

namespace erhe::commands {

class Command;
struct Input_arguments;

class Update_binding : public Command_binding
{
public:
    explicit Update_binding(Command* command);
    Update_binding();
    ~Update_binding() noexcept override;

    auto get_type() const -> Type override { return Command_binding::Type::Update; }

    virtual auto on_update() -> bool;
};


} // namespace erhe::commands

