#include "tools/material_paint_tool.hpp"

#include "editor_context.hpp"

#include "graphics/icon_set.hpp"
#include "scene/content_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_scene/mesh.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor
{

#pragma region Command

Material_paint_command::Material_paint_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "Material_paint.paint"}
    , m_context{context}
{
}

void Material_paint_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        return;
    }

    if (m_context.material_paint_tool->on_paint_ready()) {
        set_ready();
    }
}

auto Material_paint_command::try_call() -> bool
{
    if (get_command_state() == erhe::commands::State::Inactive) {
        return false;
    }

    if (
        m_context.material_paint_tool->on_paint() &&
        (get_command_state() == erhe::commands::State::Ready)
    ) {
        set_active();
    }

    return get_command_state() == erhe::commands::State::Active;
}

void Material_pick_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        return;
    }

    if (m_context.material_paint_tool->on_pick_ready()) {
        set_ready();
    }
}

Material_pick_command::Material_pick_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "Material_paint.pick"}
    , m_context{context}
{
}

auto Material_pick_command::try_call() -> bool
{
    if (get_command_state() == erhe::commands::State::Inactive) {
        return false;
    }

    if (
        m_context.material_paint_tool->on_pick() &&
        (get_command_state() == erhe::commands::State::Ready)
    ) {
       set_active();
    }

    return get_command_state() == erhe::commands::State::Active;
}
#pragma endregion Command

Material_paint_tool::Material_paint_tool(
    erhe::commands::Commands& commands,
    Editor_context&           editor_context,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool           {editor_context}
    , m_paint_command{commands, editor_context}
    , m_pick_command {commands, editor_context}
{
    set_base_priority(c_priority);
    set_description  ("Material Paint");
    set_flags        (Tool_flags::toolbox);
    set_icon         (icon_set.icons.material);
    tools.register_tool(this);

    commands.register_command(&m_paint_command);
    commands.register_command(&m_pick_command);
    commands.bind_command_to_mouse_button(&m_paint_command, erhe::window::Mouse_button_left, false);
    commands.bind_command_to_mouse_button(&m_pick_command,  erhe::window::Mouse_button_right, true);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = headset_view.get_headset();
    if (headset != nullptr) {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_paint_command, xr_right.trigger_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_paint_command, xr_right.a_click,       erhe::commands::Button_trigger::Button_pressed);
    }
#else
    static_cast<void>(headset_view);
#endif

    set_active_command(c_command_paint);

    m_paint_command.set_host(this);
    m_pick_command .set_host(this);
}

auto Material_paint_tool::on_paint_ready() -> bool
{
    const auto viewport_window = m_context.viewport_windows->hover_window();
    if (!viewport_window) {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    return hover.valid && hover.mesh;
}

auto Material_paint_tool::on_pick_ready() -> bool
{
    const auto viewport_window = m_context.viewport_windows->hover_window();
    if (viewport_window == nullptr) {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    return hover.valid && hover.mesh;
}

auto Material_paint_tool::on_paint() -> bool
{
    if (m_material == nullptr) {
        return false;
    }

    const auto viewport_window = m_context.viewport_windows->hover_window();
    if (viewport_window == nullptr) {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    if (!hover.valid || (hover.mesh == nullptr)) {
        return false;
    }
    auto& hover_primitive = hover.mesh->mesh_data.primitives.at(hover.primitive_index);
    hover_primitive.material = m_material;

    return true;
}

auto Material_paint_tool::on_pick() -> bool
{
    const auto viewport_window = m_context.viewport_windows->hover_window();
    if (viewport_window == nullptr) {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    if (!hover.valid || (hover.mesh == nullptr)) {
        return false;
    }
    auto& hover_primitive = hover.mesh->mesh_data.primitives.at(hover.primitive_index);
    m_material = hover_primitive.material;

    return true;
}

void Material_paint_tool::set_active_command(const int command)
{
    m_active_command = command;

    switch (command) {
        case c_command_paint: {
            m_paint_command.enable();
            m_pick_command.disable();
            break;
        }
        case c_command_pick: {
            m_paint_command.disable();
            m_pick_command.enable();
            break;
        }
        default: {
            break;
        }
    }
}

void Material_paint_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    if (new_priority < old_priority) {
        disable();
    }
    if (new_priority > old_priority) {
        enable();
    }
}

void Material_paint_tool::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    auto* hover_scene_view = Tool::get_hover_scene_view();
    if (hover_scene_view == nullptr) {
        return;
    }

    const auto& scene_root = hover_scene_view->get_scene_root();
    if (!scene_root) {
        return;
    }

    const auto& material_library = scene_root->content_library()->materials;
    int command = m_active_command;
    ImGui::RadioButton("Paint", &command, c_command_paint); ImGui::SameLine();
    ImGui::RadioButton("Pick",  &command, c_command_pick);
    if (command != m_active_command) {
        set_active_command(command);
    }

    material_library->combo(m_context, "Material", m_material, false);
#endif
}

} // namespace editor
