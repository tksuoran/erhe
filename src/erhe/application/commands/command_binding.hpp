#pragma once

#include "erhe/application/commands/state.hpp"

#include "erhe/toolkit/unique_id.hpp"

namespace erhe::application {

class Command;

class Command_binding
{
public:
    enum class Type : int
    {
        None               = 0,
        Key                = 1,
        Mouse              = 2,
        Mouse_click        = 3,
        Mouse_drag         = 4,
        Mouse_motion       = 5,
        Mouse_wheel        = 6,
        Controller_trigger = 7,
        Update             = 8
    };
    explicit Command_binding(Command* const command);
    virtual ~Command_binding() noexcept;

    Command_binding();
    Command_binding(const Command_binding&) = delete;
    Command_binding(Command_binding&& other) noexcept;
    auto operator=(const Command_binding&) -> Command_binding& = delete;
    auto operator=(Command_binding&& other) noexcept -> Command_binding&;

    [[nodiscard]] virtual auto get_type   () const -> Type { return Type::None; }
    [[nodiscard]] auto         get_id     () const -> erhe::toolkit::Unique_id<Command_binding>::id_type;
    [[nodiscard]] auto         get_command() const -> Command*;

private:
    Command*                                  m_command{nullptr};
    erhe::toolkit::Unique_id<Command_binding> m_id;
};

} // namespace erhe::application

