#pragma once

#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <memory>

namespace erhe::scene { class Node; }

namespace editor {

class App_context;
class App_message_bus;

// Tracks the active joint for the "Joint Weight Ramp" shader debug mode
// (Shader_debug::joint_weight_ramp) and keeps
// App_rendering::debug_joint_indices pointing at it:
//   .x = the joint's global index in the joint buffer - the joint walk
//        order of Scene::get_skins(), which is the order every
//        Joint_buffer::update() caller passes skins in - or 0xffffffffu
//        when no joint is active ("missing data" display);
//   .y = 1 to display zero-weight vertices as black (Blender's zero-weight
//        alert), 0 to show them at the ramp bottom (dark blue).
// The value is one editor-global slot applied to every scene's joint
// buffer, so with several open scenes only the active joint's own scene
// shows a meaningful highlight (same trade-off as debug_joint_colors).
//
// The active joint follows selection: selecting a joint node (bone-mode
// viewport click or the item tree; joints carry Item_flags::bone) makes it
// the active joint, and it stays active when deselected so the display
// survives clicking elsewhere. Event-driven only - selection changes and
// skin (un)registration are the only things that change the mapping, so
// there is no per-frame update.
class Weight_display
{
public:
    Weight_display(App_context& context, App_message_bus& app_message_bus);
    ~Weight_display() noexcept;

    [[nodiscard]] auto get_active_joint() const -> std::shared_ptr<erhe::scene::Node>;
    void set_active_joint(const std::shared_ptr<erhe::scene::Node>& joint);

    [[nodiscard]] auto get_show_zero_weight_black() const -> bool;
    void set_show_zero_weight_black(bool value);

    // Active joint status line, clear button and the zero-black checkbox.
    // Called from App_rendering::imgui()'s "Skin Debug" section.
    void imgui();

private:
    void on_selection      (Selection_message& message);
    void on_skin_registered(Skin_registered_message& message);
    void on_close_scene    (Close_scene_message& message);

    // Re-resolve the active joint to its global joint-buffer index and write
    // App_rendering::debug_joint_indices. Drops the active joint (writing the
    // no-joint sentinel) when it no longer belongs to any scene's skin.
    void update_debug_joint_indices();

    App_context&                     m_context;
    std::weak_ptr<erhe::scene::Node> m_active_joint;
    bool                             m_show_zero_weight_black{false};

    erhe::message_bus::Subscription<Selection_message>       m_selection_subscription;
    erhe::message_bus::Subscription<Skin_registered_message> m_skin_registered_subscription;
    erhe::message_bus::Subscription<Close_scene_message>     m_close_scene_subscription;
};

}
