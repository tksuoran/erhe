#pragma once

namespace erhe::commands {

class Command;

enum class Button_trigger : unsigned int
{
    Button_pressed = 0,
    Button_released = 1,
    Any = 2
};

auto c_str(const Button_trigger value) -> const char*;

class Command_binding
{
public:
    enum class Type : int
    {
        None              =  0,
        Key               =  1,
        Mouse             =  2,
        Mouse_button      =  3,
        Mouse_drag        =  4,
        Mouse_motion      =  5,
        Mouse_wheel       =  6,
        Menu              =  7,
        Controller_axis   =  8,
        Controller_button =  9,
        Xr_boolean        = 10,
        Xr_float          = 11,
        Xr_vector2f       = 12,
        Xr_pose           = 13,
        Update            = 14
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
        "Menu",
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

