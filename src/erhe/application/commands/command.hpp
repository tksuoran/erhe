#pragma once

#include "erhe/application/commands/state.hpp"

#include <optional>

namespace erhe::application {

class Command_context;

class Command
{
public:
    explicit Command(const char* name);
    virtual ~Command() noexcept;

    Command(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(const Command&) = delete;
    Command& operator=(Command&&) = delete;

    // Virtual interface
    [[nodiscard]] virtual auto try_call(Command_context& context) -> bool;
    virtual void try_ready  (Command_context& context);
    virtual void on_inactive(Command_context& context);

    // Non-virtual public API
    [[nodiscard]] auto state() const -> State;
    [[nodiscard]] auto name () const -> const char*;
    void disable     (Command_context& context);
    void enable      (Command_context& context);
    void set_inactive(Command_context& context);
    void set_ready   (Command_context& context);
    void set_active  (Command_context& context);

private:
    State       m_state{State::Inactive};
    const char* m_name {nullptr};
};

} // namespace erhe::application

