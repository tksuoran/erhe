#pragma once

#include "erhe_commands/mouse_binding.hpp"

namespace erhe::commands {

class Mouse_motion_binding
    : public Mouse_binding
{
public:
    explicit Mouse_motion_binding(Command* command, const std::optional<uint32_t> modifier_mask = {});
    Mouse_motion_binding();
    ~Mouse_motion_binding() noexcept override;

    auto get_type() const -> Type override { return Command_binding::Type::Mouse_motion; }
    auto on_motion(Input_arguments& input) -> bool override;
};

} // namespace erhe::commands

