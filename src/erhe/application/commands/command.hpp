#pragma once

#include "erhe/application/commands/state.hpp"

#include <optional>
#include <string>

namespace erhe::application {

class Command_context;

class Command_host
{
public:
    [[nodiscard]] virtual auto get_priority() const -> int = 0;

    [[nodiscard]] auto is_enabled     () const -> bool;
    [[nodiscard]] auto get_description() const -> const char*;
    void set_description(const std::string_view description);
    void set_enabled    (bool enabled);
    void enable         ();
    void disable        ();

private:
    std::string m_description;
    bool        m_enabled{true};
};

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
    [[nodiscard]] auto get_command_state() const -> State;
    [[nodiscard]] auto get_name         () const -> const char*;
    [[nodiscard]] auto get_priority     () const -> int;
    [[nodiscard]] auto get_host         () const -> Command_host*;
    void set_host    (Command_host* host);
    void disable     (Command_context& context);
    void enable      (Command_context& context);
    void set_inactive(Command_context& context);
    void set_ready   (Command_context& context);
    void set_active  (Command_context& context);

private:
    auto get_base_priority() const -> int;
    auto get_host_priority() const -> int;

    State         m_state{State::Inactive};
    Command_host* m_host {nullptr};
    const char*   m_name {nullptr};
};

} // namespace erhe::application

