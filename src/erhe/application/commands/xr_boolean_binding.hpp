#pragma once

#include "erhe/application/commands/command_binding.hpp"

struct XrActionStateBoolean;

namespace erhe::xr {

class Xr_action_boolean;

}

namespace erhe::application {

class Input_arguments;

class Xr_boolean_binding
    : public Command_binding
{
public:
    Xr_boolean_binding(Command* command, erhe::xr::Xr_action_boolean* xr_action);
    ~Xr_boolean_binding() noexcept override;

    Xr_boolean_binding();
    Xr_boolean_binding(const Xr_boolean_binding&) = delete;
    Xr_boolean_binding(Xr_boolean_binding&& other) noexcept;
    auto operator=(const Xr_boolean_binding&) -> Xr_boolean_binding& = delete;
    auto operator=(Xr_boolean_binding&& other) noexcept -> Xr_boolean_binding&;

    [[nodiscard]] auto get_type() const -> Type override;

    auto on_value_changed(Input_arguments& input) -> bool;

    erhe::xr::Xr_action_boolean* xr_action{nullptr};
};

} // namespace erhe/application
