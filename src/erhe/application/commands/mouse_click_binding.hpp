#pragma once

#include "erhe/application/commands/mouse_binding.hpp"

namespace erhe::application {

// Mouse pressed and released while not being moved
class Mouse_click_binding
    : public Mouse_binding
{
public:
    Mouse_click_binding(
        Command* const                    command,
        const erhe::toolkit::Mouse_button button
    );
    ~Mouse_click_binding() noexcept override;

    Mouse_click_binding();
    Mouse_click_binding(const Mouse_click_binding&) = delete;
    Mouse_click_binding(Mouse_click_binding&& other) noexcept;
    auto operator=(const Mouse_click_binding&) -> Mouse_click_binding& = delete;
    auto operator=(Mouse_click_binding&& other) noexcept -> Mouse_click_binding&;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse_click; }

    [[nodiscard]] auto get_button() const -> erhe::toolkit::Mouse_button;

    auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool override;

    auto on_motion(Command_context& context) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
};

} // namespace erhe/application
