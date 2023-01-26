#pragma once

#include "erhe/application/commands/command_binding.hpp"

namespace erhe::application {

class Command;
class Input_arguments;

// Mouse wheel event
class Mouse_wheel_binding
    : public Command_binding
{
public:
    explicit Mouse_wheel_binding(Command* const command);
    ~Mouse_wheel_binding() noexcept override;

    Mouse_wheel_binding();
    Mouse_wheel_binding(const Mouse_wheel_binding&) = delete;
    Mouse_wheel_binding(Mouse_wheel_binding&& other) noexcept;
    auto operator=(const Mouse_wheel_binding&) -> Mouse_wheel_binding& = delete;
    auto operator=(Mouse_wheel_binding&& other) noexcept -> Mouse_wheel_binding&;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse_wheel; }

    virtual auto on_wheel(Input_arguments& input) -> bool;
};

} // namespace erhe/application

