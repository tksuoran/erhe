#pragma once

#include "erhe/application/commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"

namespace erhe::application {

class Command;
union Input_arguments;

class Update_binding
    : public Command_binding
{
public:
    explicit Update_binding(Command* command);
    Update_binding();
    ~Update_binding() noexcept override;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Update; }

    virtual auto on_update() -> bool;
};


} // namespace erhe::application

