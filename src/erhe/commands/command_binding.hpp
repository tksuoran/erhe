#pragma once

#include "erhe/commands/state.hpp"

#include "erhe/toolkit/unique_id.hpp"

namespace erhe::commands {

class Command;

class Command_binding
{
public:
    enum class Type : int
    {
        None         =  0,
        Key          =  1,
        Mouse        =  2,
        Mouse_button =  3,
        Mouse_drag   =  4,
        Mouse_motion =  5,
        Mouse_wheel  =  6,
        Xr_boolean   =  7,
        Xr_float     =  8,
        Xr_vector2f  =  9,
        Xr_pose      = 10,
        Update       = 11
    };

    static constexpr const char* c_type_strings[] =
    {
        "None",
        "Key",
        "Mouse",
        "Mouse_button",
        "Mouse_drag",
        "Mouse_motion",
        "Mouse_wheel",
        "Xr_boolean",
        "Xr_float",
        "Xr_vector2f",
        "Xr_pose",
        "Update"
    };

    explicit Command_binding(Command* command);
    Command_binding();
    virtual ~Command_binding() noexcept;


    [[nodiscard]] virtual auto get_type                    () const -> Type { return Type::None; }
    [[nodiscard]] auto         get_command                 () const -> Command*;
    [[nodiscard]] auto         is_command_host_enabled     () const -> bool;
    [[nodiscard]] auto         get_command_host_description() const -> const char*;

private:
    Command* m_command{nullptr};
};

} // namespace erhe::commands

