#pragma once

#include "erhe/application/commands/command_binding.hpp"

namespace erhe::application {

class Command_context;

// Mouse pressed and released while not being moved
class Controller_trigger_binding
    : public Command_binding
{
public:
    explicit Controller_trigger_binding(Command* command);
    ~Controller_trigger_binding() noexcept override;

    Controller_trigger_binding();
    Controller_trigger_binding(const Controller_trigger_binding&) = delete;
    Controller_trigger_binding(Controller_trigger_binding&& other) noexcept;
    auto operator=(const Controller_trigger_binding&) -> Controller_trigger_binding& = delete;
    auto operator=(Controller_trigger_binding&& other) noexcept -> Controller_trigger_binding&;

    [[nodiscard]] auto get_type() const -> Type override;

    virtual auto on_trigger_value (Command_context& context) -> bool;
    virtual auto on_trigger_click (Command_context& context, bool click) -> bool;
    virtual auto on_trigger_update(Command_context& context) -> bool;

private:
    Command_binding::Type m_type{Type::Controller_trigger_click};
};

} // namespace erhe/application
