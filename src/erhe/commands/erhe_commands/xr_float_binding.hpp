#pragma once

#include "erhe_commands/command_binding.hpp"

namespace erhe::xr {
    class Xr_action_float;
}

namespace erhe::commands {

struct Input_arguments;

class Xr_float_binding
    : public Command_binding
{
public:
    Xr_float_binding(Command* command, erhe::xr::Xr_action_float* xr_action);
    Xr_float_binding();
    ~Xr_float_binding() noexcept override;

    [[nodiscard]] auto get_type() const -> Type override;

    auto on_value_changed(Input_arguments& input) -> bool;

    erhe::xr::Xr_action_float* xr_action{nullptr};
};

} // namespace erhe/application
