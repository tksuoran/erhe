#pragma once

#include "erhe_commands/command_binding.hpp"

#include "erhe_window/window_event_handler.hpp"

#include <cstdint>
#include <optional>

namespace erhe::commands {

class Command;
struct Input_arguments;

class Mouse_binding
    : public Command_binding
{
public:
    Mouse_binding(Command* command, const std::optional<uint32_t> modifier_mask = {});
    Mouse_binding();
    ~Mouse_binding() noexcept override;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse; }
    [[nodiscard]] auto get_modifier_mask() const -> std::optional<uint32_t> { return m_modifier_mask; }

    virtual auto get_button() const -> erhe::window::Mouse_button;

    virtual auto on_button(Input_arguments& input) -> bool;
    virtual auto on_motion(Input_arguments& input) -> bool;

protected:
    std::optional<uint32_t> m_modifier_mask;
};

} // namespace erhe::commands

