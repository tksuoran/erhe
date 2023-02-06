#pragma once

#include "erhe/application/commands/mouse_binding.hpp"

namespace erhe::application {

class Mouse_motion_binding
    : public Mouse_binding
{
public:
    explicit Mouse_motion_binding(Command* command);
    Mouse_motion_binding();
    ~Mouse_motion_binding() noexcept override;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse_motion; }

    auto on_motion(Input_arguments& input) -> bool override;
};

} // namespace erhe::application

