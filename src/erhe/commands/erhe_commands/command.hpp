#pragma once

#include "erhe_commands/state.hpp"

#include <functional>
#include <string>
#include <string_view>

#if defined(ERHE_XR_LIBRARY_OPENXR)
namespace erhe::xr {
    class Xr_action;
    class Xr_action_float;
    class Xr_action_vector2f;
    class Xr_action_pose;
}
#endif

namespace erhe::commands {

class Commands;
struct Input_arguments;

class Command_host
{
public:
    virtual ~Command_host() noexcept;

    [[nodiscard]] virtual auto get_priority   () const -> int;
    [[nodiscard]] auto         is_enabled     () const -> bool;
    [[nodiscard]] auto         get_description() const -> const char*;
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
    Command(Commands& commands, const std::string_view name);
    virtual ~Command() noexcept;

    Command(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(const Command&) = delete;
    Command& operator=(Command&&) = delete;

    // Virtual interface
    virtual void try_ready          ();
    virtual auto try_call           () -> bool;
    virtual auto try_call_with_input(Input_arguments& input) -> bool;
    virtual auto get_priority       () const -> int;
    virtual auto is_enabled         () const -> bool;
    virtual auto get_target_command () -> Command&;
    virtual void disable            ();
    virtual void enable             ();
    virtual void on_inactive        ();
    virtual auto get_host           () const -> Command_host*;
    virtual auto get_command_state  () const -> State;

    // Non-virtual public API
    [[nodiscard]] auto is_accepted() const -> bool;
    [[nodiscard]] auto get_name() const -> const char*;
    void set_host    (Command_host* host);
    void set_inactive();
    void set_ready   ();
    void set_active  ();

private:
    auto get_base_priority() const -> int;
    auto get_host_priority() const -> int;

    Commands&     m_commands;
    State         m_state{State::Inactive};
    Command_host* m_host {nullptr};
    std::string   m_name;
};

class Helper_command : public Command
{
public:
    Helper_command(Commands& commands, Command& target_command, const std::string& name);
    auto get_priority      () const -> int override;
    auto is_enabled        () const -> bool override;
    auto get_command_state () const -> State override;
    auto get_target_command() -> Command& override;

protected:
    Command& m_target_command;
};

class Drag_enable_command
    : public Command
    , public Command_host
{
public:
    Drag_enable_command(Commands& commands, Command& update_command);
    auto try_call_with_input(Input_arguments& input) -> bool override;

private:
    Command& m_update_command;
};

class Drag_enable_float_command
    : public Command
    , public Command_host
{
public:
    Drag_enable_float_command(Commands& commands, Command& update_command, float min_to_enable, float max_to_disable);

    auto try_call_with_input(Input_arguments& input) -> bool override;

private:
    Command& m_update_command;
    float    m_min_to_enable {0.20f};
    float    m_max_to_disable{0.10f};
};

class Float_threshold_command
    : public Command
    , public Command_host
{
public:
    Float_threshold_command(Commands& commands, Command& target_command, float min_to_activate, float max_to_deactivate);

    auto try_call_with_input(Input_arguments& input) -> bool override;

private:
    Command& m_target_command;
    float    m_min_to_activate  {0.20f};
    float    m_max_to_deactivate{0.10f};
    bool     m_is_active        {false};
};

// Has enable/disable state independent from target command
class Redirect_command : public Helper_command
{
public:
    Redirect_command(Commands& commands, Command& target_command);
    void try_ready() override;
    auto try_call () -> bool override;
};

class Drag_float_command : public Helper_command
{
public:
    Drag_float_command(Commands& commands, Command& target_command);
    auto try_call() -> bool override;
};

class Drag_vector2f_command : public Helper_command
{
public:
    Drag_vector2f_command(Commands& commands, Command& target_command);
    auto try_call() -> bool override;
    auto try_call_with_input(Input_arguments& input) -> bool override;
};

class Drag_pose_command : public Helper_command
{
public:
    Drag_pose_command(Commands& commands, Command& target_command);
    auto try_call() -> bool override;
};

class Lambda_command : public erhe::commands::Command
{
public:
    Lambda_command(Commands& commands, const std::string_view name, std::function<bool()> callback);

    auto try_call() -> bool override;

private:
    std::function<bool()> m_callback;
};

//

#if defined(ERHE_XR_LIBRARY_OPENXR)
class Xr_float_click_command : public Helper_command
{
public:
    Xr_float_click_command(Commands& commands, Command& target_command);
    void bind     (erhe::xr::Xr_action_float* xr_action_for_value);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    erhe::xr::Xr_action_float* m_xr_action_for_value{nullptr};
};

class Xr_vector2f_click_command : public Helper_command
{
public:
    Xr_vector2f_click_command(Commands& commands, Command& target_command);
    void bind     (erhe::xr::Xr_action_vector2f* xr_action_for_value);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    erhe::xr::Xr_action_vector2f* m_xr_action_for_value{nullptr};
};

class Xr_pose_click_command : public Helper_command
{
public:
    Xr_pose_click_command(Commands& commands, Command& target_command);
    void bind     (erhe::xr::Xr_action_pose* xr_action_for_value);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    erhe::xr::Xr_action_pose* m_xr_action_for_value{nullptr};
};
#endif

} // namespace erhe::commands

