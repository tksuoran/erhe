#pragma once

#include "erhe_commands/command_binding.hpp"

#include "erhe_window/window_event_handler.hpp" // Keycode

#include <optional>

namespace erhe::commands {

struct Input_arguments;

// Key pressed and / or released, selected by the trigger. The command sees
// the pressed state in Input_arguments::variant::button_pressed.
class Key_binding : public Command_binding
{
public:
    Key_binding(
        Command*                command,
        erhe::window::Keycode   code,
        Button_trigger          trigger,
        std::optional<uint32_t> modifier_mask
    );
    Key_binding();
    ~Key_binding() noexcept override;

    auto on_key(
        Input_arguments&      input,
        bool                  pressed,
        erhe::window::Keycode code,
        uint32_t              modifier_mask
    ) -> bool;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Key; }

    [[nodiscard]] auto get_keycode      () const -> erhe::window::Keycode;
    [[nodiscard]] auto get_trigger      () const -> Button_trigger;
    [[nodiscard]] auto get_modifier_mask() const -> std::optional<uint32_t>;

private:
    erhe::window::Keycode   m_code   {erhe::window::Key_unknown};
    Button_trigger          m_trigger{Button_trigger::Button_pressed};
    std::optional<uint32_t> m_modifier_mask;
};

} // namespace erhe::commands

