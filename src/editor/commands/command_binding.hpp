#pragma once

#include "commands/state.hpp"

#include "erhe/toolkit/unique_id.hpp"

namespace editor {

class Command;

class Command_binding
{
public:
    explicit Command_binding(Command* const command);
    virtual ~Command_binding() noexcept;

    Command_binding();
    Command_binding(const Command_binding&) = delete;
    Command_binding(Command_binding&& other) noexcept;
    auto operator=(const Command_binding&) -> Command_binding& = delete;
    auto operator=(Command_binding&& other) noexcept -> Command_binding&;

    [[nodiscard]] auto get_id     () const -> erhe::toolkit::Unique_id<Command_binding>::id_type;
    [[nodiscard]] auto get_command() const -> Command*;

private:
    Command*                                  m_command{nullptr};
    erhe::toolkit::Unique_id<Command_binding> m_id;
};


} // namespace Editor

