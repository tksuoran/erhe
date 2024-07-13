#pragma once

#include "erhe_commands/command_binding.hpp"

#include <cstdint>
#include <optional>

namespace erhe::commands {

struct Input_arguments;

class Controller_button_binding : public Command_binding
{
public:
    Controller_button_binding(Command* command, int button, Button_trigger trigger, const std::optional<uint32_t> modifier_mask = {});
    Controller_button_binding();
    ~Controller_button_binding() noexcept override;

    auto get_type() const -> Type override;

    auto on_value_changed(Input_arguments& input) -> bool;
    auto test_trigger    (Input_arguments& input) const -> bool;

    [[nodiscard]] auto get_button() const -> int;
    [[nodiscard]] auto get_modifier_mask() const -> const std::optional<uint32_t>&;

protected:
    int m_button;
    Button_trigger m_trigger{Button_trigger::Button_pressed};
    std::optional<uint32_t> m_modifier_mask;
};

} // namespace erhe::commands
