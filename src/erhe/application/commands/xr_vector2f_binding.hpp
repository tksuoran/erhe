#pragma once

#include "erhe/application/commands/command_binding.hpp"

namespace erhe::xr {

class Xr_action_vector2f;

}

namespace erhe::application {

class Input_arguments;

class Xr_vector2f_binding
    : public Command_binding
{
public:
    Xr_vector2f_binding();
    explicit Xr_vector2f_binding(
        Command*                      command,
        erhe::xr::Xr_action_vector2f* xr_action
    );
    ~Xr_vector2f_binding() noexcept override;
    Xr_vector2f_binding(const Xr_vector2f_binding&) = delete;
    Xr_vector2f_binding(Xr_vector2f_binding&& other) noexcept = default;
    auto operator=(const Xr_vector2f_binding&) -> Xr_vector2f_binding& = delete;
    auto operator=(Xr_vector2f_binding&& other) noexcept -> Xr_vector2f_binding& = default;

    [[nodiscard]] auto get_type() const -> Type override;

    auto on_value_changed(Input_arguments& input) -> bool;

    erhe::xr::Xr_action_vector2f* xr_action{nullptr};
};

} // namespace erhe/application
