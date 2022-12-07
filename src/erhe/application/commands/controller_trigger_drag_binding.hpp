#pragma once

#include "erhe/application/commands/controller_trigger_binding.hpp"

namespace erhe::application {

class Command_context;

// Mouse pressed and released while not being moved
class Controller_trigger_drag_binding
    : public Controller_trigger_binding
{
public:
    explicit Controller_trigger_drag_binding(Command* command);
    ~Controller_trigger_drag_binding() noexcept override;

    Controller_trigger_drag_binding();
    Controller_trigger_drag_binding(const Controller_trigger_drag_binding&) = delete;
    Controller_trigger_drag_binding(Controller_trigger_drag_binding&& other) noexcept;
    auto operator=(const Controller_trigger_drag_binding&) -> Controller_trigger_drag_binding& = delete;
    auto operator=(Controller_trigger_drag_binding&& other) noexcept -> Controller_trigger_drag_binding&;

    [[nodiscard]] auto get_type() const -> Type override;

    auto on_trigger_click (Command_context& context, bool click) -> bool override;
    auto on_trigger_update(Command_context& context) -> bool override;
};

} // namespace erhe/application
