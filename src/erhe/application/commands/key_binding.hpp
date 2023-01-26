#pragma once

#include "erhe/application/commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"

#include <optional>

namespace erhe::application {

class Input_arguments;

// Key pressesed or released
class Key_binding
    : public Command_binding
{
public:
    Key_binding(
        Command* const                command,
        const erhe::toolkit::Keycode  code,
        const bool                    pressed,
        const std::optional<uint32_t> modifier_mask
    );
    ~Key_binding() noexcept override;

    Key_binding();
    Key_binding(const Key_binding&) = delete;
    Key_binding(Key_binding&& other) noexcept;
    auto operator=(const Key_binding&) -> Key_binding& = delete;
    auto operator=(Key_binding&& other) noexcept -> Key_binding&;

    auto on_key(
        Input_arguments&             input,
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    ) -> bool;

    [[nodiscard]] auto get_type   () const -> Type override { return Command_binding::Type::Key; }
    [[nodiscard]] auto get_keycode() const -> erhe::toolkit::Keycode;
    [[nodiscard]] auto get_pressed() const -> bool;

private:
    erhe::toolkit::Keycode  m_code         {erhe::toolkit::Key_unknown};
    bool                    m_pressed      {true};
    std::optional<uint32_t> m_modifier_mask;
};

} // namespace erhe::application

