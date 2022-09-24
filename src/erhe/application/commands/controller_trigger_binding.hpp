#pragma once

#include "erhe/application/commands/command_binding.hpp"

namespace erhe::application {

class Command_context;

// Mouse pressed and released while not being moved
class Controller_trigger_binding
    : public Command_binding
{
public:
    Controller_trigger_binding(
        Command* command,
        float    min_to_activate,
        float    max_to_deactivate
    );
    ~Controller_trigger_binding() noexcept override;

    Controller_trigger_binding();
    Controller_trigger_binding(const Controller_trigger_binding&) = delete;
    Controller_trigger_binding(Controller_trigger_binding&& other) noexcept;
    auto operator=(const Controller_trigger_binding&) -> Controller_trigger_binding& = delete;
    auto operator=(Controller_trigger_binding&& other) noexcept -> Controller_trigger_binding&;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Controller_trigger; }

    [[nodiscard]] auto get_min_to_activate() const -> float;
    [[nodiscard]] auto get_max_to_deactivate() const -> float;

    auto on_trigger(
        Command_context& context,
        const float      trigger_value
    ) -> bool;

private:
    float m_min_to_activate  {0.50f};
    float m_max_to_deactivate{0.45f};
    bool  m_active{false};
};

} // namespace erhe/application
