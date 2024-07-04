#pragma once

#include "erhe_commands/command_binding.hpp"

#include <cstdint>
#include <optional>

namespace erhe::commands {

class Command;
struct Input_arguments;

// Mouse wheel event
class Mouse_wheel_binding
    : public Command_binding
{
public:
    explicit Mouse_wheel_binding(Command* command, const std::optional<uint32_t> modifier_mask = {});
    Mouse_wheel_binding();
    ~Mouse_wheel_binding() noexcept override;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse_wheel; }

    virtual auto on_wheel(Input_arguments& input) -> bool;

private:
    std::optional<uint32_t> m_modifier_mask;
};

} // namespace erhe/application

