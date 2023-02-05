#pragma once

#include "erhe/application/commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"

namespace erhe::application {

class Command;
union Input_arguments;

class Mouse_binding
    : public Command_binding
{
public:
    explicit Mouse_binding(Command* const command);
    Mouse_binding();
    ~Mouse_binding() noexcept override;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse; }

    virtual [[nodiscard]] auto get_button() const -> erhe::toolkit::Mouse_button;

    virtual auto on_button(Input_arguments& input) -> bool;
    virtual auto on_motion(Input_arguments& input) -> bool;
};


} // namespace erhe::application

