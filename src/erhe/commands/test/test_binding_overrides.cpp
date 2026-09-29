#include "erhe_commands/binding_desc.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/state.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using erhe::commands::Binding_desc;
using erhe::commands::Binding_kind;
using erhe::commands::Binding_override;
using erhe::commands::Button_trigger;
using erhe::commands::Commands;
using erhe::commands::Input_kind;

class Counting_command : public erhe::commands::Command
{
public:
    Counting_command(Commands& commands, const std::string_view name)
        : Command{commands, name}
    {
    }

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override
    {
        ++call_count;
        last_pressed = input.variant.button_pressed;
        return true;
    }

    int  call_count  {0};
    bool last_pressed{false};
};

class Drag_command : public erhe::commands::Command
{
public:
    Drag_command(Commands& commands, const std::string_view name)
        : Command{commands, name}
    {
    }

    auto try_call_with_input(erhe::commands::Input_arguments&) -> bool override
    {
        return true;
    }
};

// Calls back into Commands from within dispatch, as a tool switch does
// (Tools::set_priority_tool() -> Commands::sort_bindings()).
class Resorting_command : public erhe::commands::Command
{
public:
    Resorting_command(Commands& commands, const std::string_view name)
        : Command{commands, name}
        , m_commands{commands}
    {
    }

    auto try_call_with_input(erhe::commands::Input_arguments&) -> bool override
    {
        ++call_count;
        m_commands.sort_bindings();
        return true;
    }

    int call_count{0};

private:
    Commands& m_commands;
};

auto key_event(const erhe::window::Keycode keycode, const bool pressed, const uint32_t modifier_mask = 0) -> erhe::window::Input_event
{
    erhe::window::Input_event event{};
    event.type = erhe::window::Input_event_type::key_event;
    event.u.key_event = erhe::window::Key_event{.keycode = keycode, .modifier_mask = modifier_mask, .pressed = pressed};
    return event;
}

auto mouse_button_event(const erhe::window::Mouse_button button, const bool pressed) -> erhe::window::Input_event
{
    erhe::window::Input_event event{};
    event.type = erhe::window::Input_event_type::mouse_button_event;
    event.u.mouse_button_event = erhe::window::Mouse_button_event{.button = button, .pressed = pressed, .modifier_mask = 0};
    return event;
}

auto mouse_move_event(const float x, const float y, const float dx, const float dy) -> erhe::window::Input_event
{
    erhe::window::Input_event event{};
    event.type = erhe::window::Input_event_type::mouse_move_event;
    event.u.mouse_move_event = erhe::window::Mouse_move_event{.x = x, .y = y, .dx = dx, .dy = dy, .modifier_mask = 0};
    return event;
}

void send(Commands& commands, const erhe::window::Input_event& event)
{
    std::vector<erhe::window::Input_event> events{event};
    commands.tick(0, events);
}

void press_and_release(Commands& commands, const erhe::window::Keycode keycode, const uint32_t modifier_mask = 0)
{
    send(commands, key_event(keycode, true, modifier_mask));
    send(commands, key_event(keycode, false, modifier_mask));
}

auto key(const erhe::window::Keycode keycode, const std::optional<uint32_t> modifier_mask = 0u) -> Binding_desc
{
    return Binding_desc{.kind = Binding_kind::key, .code = keycode, .modifier_mask = modifier_mask};
}

} // anonymous namespace

TEST(Binding_overrides, defaults_dispatch_until_overridden)
{
    Commands commands;
    Counting_command command{commands, "Test.command"};
    commands.register_command(&command);
    commands.bind_command_to_key(&command, erhe::window::Key_a);

    press_and_release(commands, erhe::window::Key_a);
    EXPECT_EQ(command.call_count, 1);
    EXPECT_FALSE(commands.has_binding_override(command));

    const std::vector<Binding_desc> replacement{key(erhe::window::Key_b, erhe::window::Key_modifier_bit_ctrl)};
    ASSERT_TRUE(commands.set_binding_override(command, replacement));
    EXPECT_TRUE(commands.has_binding_override(command));

    press_and_release(commands, erhe::window::Key_a);
    EXPECT_EQ(command.call_count, 1); // default replaced
    press_and_release(commands, erhe::window::Key_b);
    EXPECT_EQ(command.call_count, 1); // modifiers must match exactly
    press_and_release(commands, erhe::window::Key_b, erhe::window::Key_modifier_bit_ctrl);
    EXPECT_EQ(command.call_count, 2);

    std::vector<Binding_desc> defaults;
    commands.get_default_bindings(command, defaults);
    ASSERT_EQ(defaults.size(), 1u);
    EXPECT_EQ(defaults[0].code, erhe::window::Key_a);
    std::vector<Binding_desc> effective;
    commands.get_effective_bindings(command, effective);
    ASSERT_EQ(effective.size(), 1u);
    EXPECT_EQ(effective[0], replacement[0]);

    commands.clear_binding_override(command);
    EXPECT_FALSE(commands.has_binding_override(command));
    press_and_release(commands, erhe::window::Key_a);
    EXPECT_EQ(command.call_count, 3);
}

TEST(Binding_overrides, empty_override_unbinds)
{
    Commands commands;
    Counting_command command{commands, "Test.command"};
    commands.register_command(&command);
    commands.bind_command_to_key(&command, erhe::window::Key_a);

    ASSERT_TRUE(commands.set_binding_override(command, std::span<const Binding_desc>{}));
    press_and_release(commands, erhe::window::Key_a);
    EXPECT_EQ(command.call_count, 0);

    std::vector<Binding_override> overrides;
    commands.get_binding_overrides(overrides);
    ASSERT_EQ(overrides.size(), 1u);
    EXPECT_EQ(overrides[0].command_name, "Test.command");
    EXPECT_TRUE(overrides[0].bindings.empty());

    commands.clear_all_binding_overrides();
    press_and_release(commands, erhe::window::Key_a);
    EXPECT_EQ(command.call_count, 1);
}

TEST(Binding_overrides, any_trigger_sees_press_and_release)
{
    Commands commands;
    Counting_command command{commands, "Test.hold"};
    commands.register_command(&command);
    commands.bind_command_to_key(&command, erhe::window::Key_w, Button_trigger::Any);

    send(commands, key_event(erhe::window::Key_w, true));
    EXPECT_EQ(command.call_count, 1);
    EXPECT_TRUE(command.last_pressed);
    send(commands, key_event(erhe::window::Key_w, false));
    EXPECT_EQ(command.call_count, 2);
    EXPECT_FALSE(command.last_pressed);
}

TEST(Binding_overrides, input_kind_is_enforced)
{
    Commands commands;
    Counting_command key_command {commands, "Test.key"};
    Drag_command     drag_command{commands, "Test.drag"};
    Counting_command menu_command{commands, "Test.menu"};
    Counting_command internal    {commands, "Test.internal"};
    commands.register_command(&key_command);
    commands.register_command(&drag_command);
    commands.register_command(&menu_command);
    commands.register_command(&internal);
    commands.bind_command_to_key(&key_command, erhe::window::Key_a);
    commands.bind_command_to_mouse_drag(&drag_command, erhe::window::Mouse_button_right, true);
    commands.bind_command_to_menu(&menu_command, "Test.Menu");

    EXPECT_EQ(commands.get_input_kind(key_command),  Input_kind::button);
    EXPECT_EQ(commands.get_input_kind(drag_command), Input_kind::drag);
    EXPECT_EQ(commands.get_input_kind(menu_command), Input_kind::button);
    EXPECT_EQ(commands.get_input_kind(internal),     Input_kind::internal);

    const Binding_desc drag{.kind = Binding_kind::mouse_drag, .code = static_cast<int>(erhe::window::Mouse_button_left), .modifier_mask = 0u};
    std::string error;
    EXPECT_FALSE(commands.set_binding_override(key_command, std::span<const Binding_desc>{&drag, 1}, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(commands.has_binding_override(key_command));

    const Binding_desc key_f2 = key(erhe::window::Key_f2);
    EXPECT_FALSE(commands.set_binding_override(internal, std::span<const Binding_desc>{&key_f2, 1}));

    // Menu-only commands can be given keys.
    ASSERT_TRUE(commands.set_binding_override(menu_command, std::span<const Binding_desc>{&key_f2, 1}));
    press_and_release(commands, erhe::window::Key_f2);
    EXPECT_EQ(menu_command.call_count, 1);
    ASSERT_EQ(commands.get_menu_bindings().size(), 1u);
    EXPECT_EQ(commands.get_menu_bindings()[0].get_shortcut_label(), "F2");

    // A user drag binding takes the command's drag semantics from its default.
    ASSERT_TRUE(commands.set_binding_override(drag_command, std::span<const Binding_desc>{&drag, 1}));
    std::vector<Binding_desc> effective;
    commands.get_effective_bindings(drag_command, effective);
    ASSERT_EQ(effective.size(), 1u);
    EXPECT_TRUE(effective[0].drag_call_on_button_down_without_motion);
}

TEST(Binding_overrides, unknown_commands_round_trip)
{
    Commands commands;
    Counting_command command{commands, "Test.command"};
    commands.register_command(&command);
    commands.bind_command_to_key(&command, erhe::window::Key_a);

    const std::vector<Binding_override> loaded{
        Binding_override{.command_name = "Test.command", .bindings = {key(erhe::window::Key_c)}},
        Binding_override{.command_name = "Other.build_only", .bindings = {key(erhe::window::Key_d)}},
        Binding_override{.command_name = "Other.unbound", .bindings = {}}
    };
    commands.apply_binding_overrides(loaded);

    std::vector<Binding_override> saved;
    commands.get_binding_overrides(saved);
    ASSERT_EQ(saved.size(), 3u);
    EXPECT_EQ(saved[0].command_name, "Test.command");
    EXPECT_EQ(saved[1].command_name, "Other.build_only");
    EXPECT_EQ(saved[1].bindings[0], key(erhe::window::Key_d));

    press_and_release(commands, erhe::window::Key_c);
    EXPECT_EQ(command.call_count, 1);
}

TEST(Binding_overrides, conflicts_are_reported)
{
    Commands commands;
    Counting_command first {commands, "Test.first"};
    Counting_command second{commands, "Test.second"};
    commands.register_command(&first);
    commands.register_command(&second);
    commands.bind_command_to_key(&first,  erhe::window::Key_a);
    commands.bind_command_to_key(&second, erhe::window::Key_b, Button_trigger::Button_pressed, erhe::window::Key_modifier_bit_ctrl);
    commands.sort_bindings();
    EXPECT_TRUE(commands.get_binding_conflicts().empty());

    // Default modifier mask is "any", so Ctrl+A also fires the first command.
    const Binding_desc ctrl_a = key(erhe::window::Key_a, erhe::window::Key_modifier_bit_ctrl);
    ASSERT_TRUE(commands.set_binding_override(second, std::span<const Binding_desc>{&ctrl_a, 1}));
    commands.sort_bindings();
    ASSERT_EQ(commands.get_binding_conflicts().size(), 2u);
    EXPECT_EQ(commands.get_binding_conflicts()[0].command,       &first);
    EXPECT_EQ(commands.get_binding_conflicts()[0].other_command, &second);
}

TEST(Binding_overrides, sort_bindings_from_dispatched_command)
{
    Commands commands;
    Resorting_command command{commands, "Test.resort"};
    commands.register_command(&command);
    commands.bind_command_to_mouse_button(&command, erhe::window::Mouse_button_left, Button_trigger::Button_pressed);

    // Two presses in one tick: the re-entrant sort_bindings() must neither
    // throw (recursive lock) nor disturb the dispatch of the second event.
    std::vector<erhe::window::Input_event> events{
        mouse_button_event(erhe::window::Mouse_button_left, true),
        mouse_button_event(erhe::window::Mouse_button_left, false),
        mouse_button_event(erhe::window::Mouse_button_left, true),
        mouse_button_event(erhe::window::Mouse_button_left, false)
    };
    commands.tick(0, events);
    EXPECT_EQ(command.call_count, 2);
}

TEST(Binding_overrides, rebind_mid_drag_inactivates_command)
{
    Commands commands;
    Drag_command command{commands, "Test.drag"};
    commands.register_command(&command);
    commands.bind_command_to_mouse_drag(&command, erhe::window::Mouse_button_left, false);

    send(commands, mouse_button_event(erhe::window::Mouse_button_left, true));
    EXPECT_EQ(command.get_command_state(), erhe::commands::State::Ready);
    send(commands, mouse_move_event(10.0f, 10.0f, 5.0f, 0.0f));
    EXPECT_EQ(command.get_command_state(), erhe::commands::State::Active);
    EXPECT_EQ(commands.get_active_mouse_command(), &command);

    const Binding_desc right_drag{.kind = Binding_kind::mouse_drag, .code = static_cast<int>(erhe::window::Mouse_button_right), .modifier_mask = 0u};
    ASSERT_TRUE(commands.set_binding_override(command, std::span<const Binding_desc>{&right_drag, 1}));
    send(commands, mouse_move_event(12.0f, 10.0f, 2.0f, 0.0f));
    EXPECT_EQ(command.get_command_state(), erhe::commands::State::Inactive);
    EXPECT_EQ(commands.get_active_mouse_command(), nullptr);
    send(commands, mouse_button_event(erhe::window::Mouse_button_left, false));
}
