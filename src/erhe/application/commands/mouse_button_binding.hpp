#pragma once

#include "erhe/application/commands/mouse_binding.hpp"

namespace erhe::application {

// Mouse pressed and released while not being moved
class Mouse_button_binding
    : public Mouse_binding
{
public:
    Mouse_button_binding(
        Command* const                    command,
        const erhe::toolkit::Mouse_button button
    );
    ~Mouse_button_binding() noexcept override;

    Mouse_button_binding();
    Mouse_button_binding(const Mouse_button_binding&) = delete;
    Mouse_button_binding(Mouse_button_binding&& other) noexcept;
    auto operator=(const Mouse_button_binding&) -> Mouse_button_binding& = delete;
    auto operator=(Mouse_button_binding&& other) noexcept -> Mouse_button_binding&;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse_click; }

    [[nodiscard]] auto get_button() const -> erhe::toolkit::Mouse_button;

    auto on_button(
        Input_arguments&            input,
        erhe::toolkit::Mouse_button button,
        bool                        pressed
    ) -> bool override;

    auto on_motion(Input_arguments& input) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
};

} // namespace erhe/application
