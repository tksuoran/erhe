#pragma once

#include "erhe/application/commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"

namespace erhe::application {

class Command;
class Command_context;

class Mouse_binding
    : public Command_binding
{
public:
    explicit Mouse_binding(Command* const command);
    ~Mouse_binding() noexcept override;

    Mouse_binding();
    Mouse_binding(const Mouse_binding&) = delete;
    Mouse_binding(Mouse_binding&& other) noexcept;
    auto operator=(const Mouse_binding&) -> Mouse_binding& = delete;
    auto operator=(Mouse_binding&& other) noexcept -> Mouse_binding&;

    virtual auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool;

    virtual auto on_motion(Command_context& context) -> bool;
};


} // namespace erhe::application

