#include "erhe_commands/binding_desc.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>

namespace {

using erhe::commands::Binding_desc;
using erhe::commands::Binding_kind;
using erhe::commands::Button_trigger;

void expect_round_trip(const Binding_desc& desc)
{
    const std::string text = desc.to_string();
    const std::optional<Binding_desc> parsed = Binding_desc::parse(text);
    ASSERT_TRUE(parsed.has_value()) << text;
    EXPECT_EQ(parsed.value(), desc) << text;
    EXPECT_EQ(parsed.value().to_string(), text);
}

const std::array<std::optional<uint32_t>, 6> c_masks{
    std::optional<uint32_t>{},
    std::optional<uint32_t>{0u},
    std::optional<uint32_t>{erhe::window::Key_modifier_bit_ctrl},
    std::optional<uint32_t>{erhe::window::Key_modifier_bit_shift | erhe::window::Key_modifier_bit_menu},
    std::optional<uint32_t>{erhe::window::Key_modifier_bit_super},
    std::optional<uint32_t>{
        erhe::window::Key_modifier_bit_ctrl | erhe::window::Key_modifier_bit_shift |
        erhe::window::Key_modifier_bit_menu | erhe::window::Key_modifier_bit_super
    }
};

} // anonymous namespace

TEST(Binding_desc, key_round_trip_every_keycode)
{
    int named_key_count = 0;
    for (erhe::window::Keycode code = 0; code <= erhe::window::Key_last; ++code) {
        if (std::string{erhe::window::c_str(code)} == "?") {
            continue;
        }
        ++named_key_count;
        for (const std::optional<uint32_t>& mask : c_masks) {
            for (const Button_trigger trigger : {Button_trigger::Button_pressed, Button_trigger::Button_released, Button_trigger::Any}) {
                expect_round_trip(Binding_desc{.kind = Binding_kind::key, .code = code, .modifier_mask = mask, .trigger = trigger});
            }
        }
    }
    EXPECT_GT(named_key_count, 100);
}

TEST(Binding_desc, mouse_round_trip)
{
    for (erhe::window::Mouse_button button = 0; button < erhe::window::Mouse_button_count; ++button) {
        for (const std::optional<uint32_t>& mask : c_masks) {
            const int code = static_cast<int>(button);
            expect_round_trip(Binding_desc{.kind = Binding_kind::mouse_button, .code = code, .modifier_mask = mask, .trigger = Button_trigger::Button_pressed});
            expect_round_trip(Binding_desc{.kind = Binding_kind::mouse_button, .code = code, .modifier_mask = mask, .trigger = Button_trigger::Button_released});
            expect_round_trip(Binding_desc{.kind = Binding_kind::mouse_drag,   .code = code, .modifier_mask = mask});
        }
    }
    for (const std::optional<uint32_t>& mask : c_masks) {
        expect_round_trip(Binding_desc{.kind = Binding_kind::mouse_wheel,  .modifier_mask = mask});
        expect_round_trip(Binding_desc{.kind = Binding_kind::mouse_motion, .modifier_mask = mask});
    }
}

TEST(Binding_desc, controller_round_trip)
{
    for (int index = 0; index < 12; ++index) {
        for (const std::optional<uint32_t>& mask : c_masks) {
            expect_round_trip(Binding_desc{.kind = Binding_kind::controller_axis,   .code = index, .modifier_mask = mask});
            expect_round_trip(Binding_desc{.kind = Binding_kind::controller_button, .code = index, .modifier_mask = mask, .trigger = Button_trigger::Any});
        }
    }
}

TEST(Binding_desc, text_form)
{
    EXPECT_EQ(
        (Binding_desc{.kind = Binding_kind::key, .code = erhe::window::Key_x, .modifier_mask = erhe::window::Key_modifier_bit_ctrl}).to_string(),
        "key:ctrl+x"
    );
    EXPECT_EQ(
        (Binding_desc{.kind = Binding_kind::key, .code = erhe::window::Key_w, .modifier_mask = {}, .trigger = Button_trigger::Any}).to_string(),
        "key:any+w:any"
    );
    EXPECT_EQ(
        (Binding_desc{.kind = Binding_kind::key, .code = erhe::window::Key_page_up, .modifier_mask = 0u}).to_string(),
        "key:page_up"
    );
    EXPECT_EQ(
        (Binding_desc{.kind = Binding_kind::mouse_drag, .code = static_cast<int>(erhe::window::Mouse_button_right), .modifier_mask = erhe::window::Key_modifier_bit_menu}).to_string(),
        "mouse_drag:alt+right"
    );
    EXPECT_EQ((Binding_desc{.kind = Binding_kind::mouse_wheel, .modifier_mask = {}}).to_string(), "mouse_wheel:any");
    EXPECT_EQ((Binding_desc{.kind = Binding_kind::mouse_wheel, .modifier_mask = 0u}).to_string(), "mouse_wheel");

    const std::optional<Binding_desc> pressed = Binding_desc::parse("key:shift+ctrl+f5:pressed");
    ASSERT_TRUE(pressed.has_value());
    EXPECT_EQ(pressed->code, erhe::window::Key_f5);
    EXPECT_EQ(pressed->modifier_mask, erhe::window::Key_modifier_bit_ctrl | erhe::window::Key_modifier_bit_shift);
    EXPECT_EQ(pressed->trigger, Button_trigger::Button_pressed);
    EXPECT_EQ(pressed->to_string(), "key:ctrl+shift+f5");
}

TEST(Binding_desc, display_string)
{
    EXPECT_EQ(
        (Binding_desc{.kind = Binding_kind::key, .code = erhe::window::Key_x, .modifier_mask = erhe::window::Key_modifier_bit_ctrl}).to_display_string(),
        "Ctrl+X"
    );
    EXPECT_EQ(
        (Binding_desc{.kind = Binding_kind::key, .code = erhe::window::Key_page_down, .modifier_mask = {}}).to_display_string(),
        "Page Down"
    );
    EXPECT_EQ(
        (Binding_desc{.kind = Binding_kind::mouse_drag, .code = static_cast<int>(erhe::window::Mouse_button_right), .modifier_mask = erhe::window::Key_modifier_bit_menu}).to_display_string(),
        "Alt+Right Drag"
    );
}

TEST(Binding_desc, rejects_malformed)
{
    for (const char* text : {
        "",
        "key",
        "key:",
        "key:ctrl+",
        "key:+x",
        "key:nosuchkey",
        "key:hyper+x",
        "key:any+ctrl+x",
        "key:ctrl+any+x",
        "key:x:sometimes",
        "key:x:pressed:extra",
        "keyboard:x",
        "mouse_button:left:any",
        "mouse_button:fourth",
        "mouse_drag:right:released",
        "mouse_wheel:shift:any",
        "mouse_wheel:x",
        "controller_axis:-1",
        "controller_axis:1x",
        "controller_axis:",
        "controller_button:a"
    }) {
        EXPECT_FALSE(Binding_desc::parse(text).has_value()) << text;
    }
}

TEST(Binding_desc, input_kind)
{
    using erhe::commands::Input_kind;
    EXPECT_EQ(erhe::commands::get_input_kind(Binding_kind::key),               Input_kind::button);
    EXPECT_EQ(erhe::commands::get_input_kind(Binding_kind::mouse_button),      Input_kind::button);
    EXPECT_EQ(erhe::commands::get_input_kind(Binding_kind::controller_button), Input_kind::button);
    EXPECT_EQ(erhe::commands::get_input_kind(Binding_kind::mouse_drag),        Input_kind::drag);
    EXPECT_EQ(erhe::commands::get_input_kind(Binding_kind::mouse_wheel),       Input_kind::wheel);
    EXPECT_EQ(erhe::commands::get_input_kind(Binding_kind::mouse_motion),      Input_kind::motion);
    EXPECT_EQ(erhe::commands::get_input_kind(Binding_kind::controller_axis),   Input_kind::axis);
}
