#pragma once

#include "erhe/application/commands/command_binding.hpp"

struct XrActionStateBoolean;

namespace erhe::xr {

class Xr_action_boolean;

}

namespace erhe::application {

union Input_arguments;

enum class Button_trigger : unsigned int
{
    Button_pressed = 0,
    Button_released = 1,
    Any = 2
};

class Xr_boolean_binding
    : public Command_binding
{
public:
    Xr_boolean_binding(
        Command*                     command,
        erhe::xr::Xr_action_boolean* xr_action,
        Button_trigger               trigger
    );
    Xr_boolean_binding();
    ~Xr_boolean_binding() noexcept override;

    [[nodiscard]] auto get_type() const -> Type override;

    auto on_value_changed(Input_arguments& input) -> bool;
    auto test_trigger    (Input_arguments& input) const -> bool;

    erhe::xr::Xr_action_boolean* xr_action{nullptr};

protected:
    Button_trigger m_trigger{Button_trigger::Button_pressed};
};

} // namespace erhe/application
