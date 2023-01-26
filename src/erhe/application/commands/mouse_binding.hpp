#pragma once

#include "erhe/application/commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"

namespace erhe::application {

class Command;
class Input_arguments;

class Mouse_binding
    : public Command_binding
{
public:
    explicit Mouse_binding(Command* const command);
    ~Mouse_binding() noexcept override;

    Mouse_binding();
    Mouse_binding(const Mouse_binding&) = delete;
    Mouse_binding(Mouse_binding&& other) noexcept;
    auto operator=(const Mouse_binding&) -> Mouse_binding& = delete;
    auto operator=(Mouse_binding&& other) noexcept -> Mouse_binding&;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse; }

    virtual auto on_button(
        Input_arguments&            input,
        erhe::toolkit::Mouse_button button,
        bool                        pressed
    ) -> bool;

    virtual auto on_motion(Input_arguments& input) -> bool;
};


} // namespace erhe::application

