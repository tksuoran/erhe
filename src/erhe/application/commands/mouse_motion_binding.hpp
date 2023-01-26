#pragma once

#include "erhe/application/commands/mouse_binding.hpp"

namespace erhe::application {

class Mouse_motion_binding
    : public Mouse_binding
{
public:
    explicit Mouse_motion_binding(Command* const command);
    ~Mouse_motion_binding() noexcept override;

    Mouse_motion_binding();
    Mouse_motion_binding(const Mouse_motion_binding&) = delete;
    Mouse_motion_binding(Mouse_motion_binding&& other) noexcept;
    auto operator=(const Mouse_motion_binding&) -> Mouse_motion_binding& = delete;
    auto operator=(Mouse_motion_binding&& other) noexcept -> Mouse_motion_binding&;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse_motion; }

    auto on_motion(Input_arguments& input) -> bool override;
};

} // namespace erhe::application

