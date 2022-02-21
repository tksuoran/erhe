#pragma once

#include "commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/optional.hpp"

namespace editor {

class Command_context;

// Key pressesed or released
class Key_binding
    : public Command_binding
{
public:
    Key_binding(
        Command* const                   command,
        const erhe::toolkit::Keycode     code,
        const bool                       pressed,
        const nonstd::optional<uint32_t> modifier_mask
    );

    auto on_key(
        Command_context&             context,
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    ) -> bool;

private:
    erhe::toolkit::Keycode     m_code         {erhe::toolkit::Key_unknown};
    bool                       m_pressed      {true};
    nonstd::optional<uint32_t> m_modifier_mask;
};

} // namespace Editor

