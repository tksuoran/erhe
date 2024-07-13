#pragma once

#include "erhe_commands/command_binding.hpp"

#include <cstdint>
#include <optional>

namespace erhe::commands {

struct Input_arguments;

class Controller_axis_binding : public Command_binding
{
public:
    Controller_axis_binding(Command* command, int axis, const std::optional<uint32_t> modifier_mask = {});
    Controller_axis_binding();
    ~Controller_axis_binding() noexcept override;

    auto get_type() const -> Type override;

    auto on_value_changed(Input_arguments& input) -> bool;

    [[nodiscard]] auto get_axis() const -> int;
    [[nodiscard]] auto get_modifier_mask() const -> const std::optional<uint32_t>&;

private:
    int m_axis;
    std::optional<uint32_t> m_modifier_mask;
};

} // namespace erhe::commands
