#pragma once

#include "erhe/application/commands/command_binding.hpp"

namespace erhe::xr {

class Xr_action_float;

}

namespace erhe::application {

class Input_arguments;

class Xr_float_binding
    : public Command_binding
{
public:
    Xr_float_binding(Command* command, erhe::xr::Xr_action_float* xr_action);
    ~Xr_float_binding() noexcept override;

    Xr_float_binding();
    Xr_float_binding(const Xr_float_binding&) = delete;
    Xr_float_binding(Xr_float_binding&& other) noexcept;
    auto operator=(const Xr_float_binding&) -> Xr_float_binding& = delete;
    auto operator=(Xr_float_binding&& other) noexcept -> Xr_float_binding&;

    [[nodiscard]] auto get_type() const -> Type override;

    auto on_value_changed(Input_arguments& input) -> bool;

    erhe::xr::Xr_action_float* xr_action{nullptr};
};

} // namespace erhe/application
