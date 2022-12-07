#pragma once

#include "erhe/application/commands/command_binding.hpp"

namespace erhe::application {

class Command_context;

class Controller_trackpad_binding
    : public Command_binding
{
public:
    Controller_trackpad_binding();
    explicit Controller_trackpad_binding(
        Command*              command,
        Command_binding::Type type
    );
    ~Controller_trackpad_binding() noexcept override;
    Controller_trackpad_binding(const Controller_trackpad_binding&) = delete;
    Controller_trackpad_binding(Controller_trackpad_binding&& other) noexcept = default;
    auto operator=(const Controller_trackpad_binding&) -> Controller_trackpad_binding& = delete;
    auto operator=(Controller_trackpad_binding&& other) noexcept -> Controller_trackpad_binding& = default;

    [[nodiscard]] auto get_type() const -> Type override;

    auto on_trackpad(Command_context& context) -> bool;

private:
    Command_binding::Type m_type{Type::Controller_trigger_click};
};

} // namespace erhe/application
