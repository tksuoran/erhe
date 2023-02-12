#include "tools/material_paint_tool.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"

#include "graphics/icon_set.hpp"
#include "scene/material_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "windows/operations.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

#pragma region Command

Material_paint_command::Material_paint_command()
    : Command{"Material_paint.paint"}
{
}

void Material_paint_command::try_ready()
{
    if (get_command_state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (g_material_paint_tool->on_paint_ready())
    {
        set_ready();
    }
}

auto Material_paint_command::try_call() -> bool
{
    if (get_command_state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        g_material_paint_tool->on_paint() &&
        (get_command_state() == erhe::application::State::Ready)
    )
    {
        set_active();
    }

    return get_command_state() == erhe::application::State::Active;
}

void Material_pick_command::try_ready()
{
    if (get_command_state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (g_material_paint_tool->on_pick_ready())
    {
        set_ready();
    }
}

Material_pick_command::Material_pick_command()
    : Command{"Material_paint.pick"}
{
}

auto Material_pick_command::try_call() -> bool
{
    if (get_command_state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        g_material_paint_tool->on_pick() &&
        (get_command_state() == erhe::application::State::Ready)
    )
    {
       set_active();
    }

    return get_command_state() == erhe::application::State::Active;
}
#pragma endregion Command

Material_paint_tool* g_material_paint_tool{nullptr};

Material_paint_tool::Material_paint_tool()
    : erhe::components::Component{c_type_name}
{
}

Material_paint_tool::~Material_paint_tool()
{
    ERHE_VERIFY(g_material_paint_tool == nullptr);
}

void Material_paint_tool::deinitialize_component()
{
    ERHE_VERIFY(g_material_paint_tool == this);
    m_paint_command.set_host(nullptr);
    m_pick_command .set_host(nullptr);
    m_material.reset();
    g_material_paint_tool = nullptr;
}

void Material_paint_tool::declare_required_components()
{
    require<erhe::application::Commands>();
    require<Icon_set  >();
    require<Operations>();
    require<Tools     >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    require<Headset_view>();
#endif
}

void Material_paint_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_material_paint_tool == nullptr);

    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox);
    set_icon         (g_icon_set->icons.material);
    g_tools->register_tool(this);

    auto& commands = *erhe::application::g_commands;
    commands.register_command(&m_paint_command);
    commands.register_command(&m_pick_command);
    commands.bind_command_to_mouse_button(&m_paint_command, erhe::toolkit::Mouse_button_left, false);
    commands.bind_command_to_mouse_button(&m_pick_command,  erhe::toolkit::Mouse_button_right, true);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = g_headset_view->get_headset();
    if (headset != nullptr)
    {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_paint_command, xr_right.trigger_click, erhe::application::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_paint_command, xr_right.a_click, erhe::application::Button_trigger::Button_pressed);
    }
#endif

    set_active_command(c_command_paint);

    m_paint_command.set_host(this);
    m_pick_command .set_host(this);

    g_material_paint_tool = this;
}

auto Material_paint_tool::on_paint_ready() -> bool
{
    const auto viewport_window = g_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    return hover.valid && hover.mesh;
}

auto Material_paint_tool::on_pick_ready() -> bool
{
    const auto viewport_window = g_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    return hover.valid && hover.mesh;
}

auto Material_paint_tool::on_paint() -> bool
{
    if (m_material == nullptr)
    {
        return false;
    }

    const auto viewport_window = g_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    if (!hover.valid || !hover.mesh)
    {
        return false;
    }
    const auto& target_mesh      = hover.mesh;
    const auto  target_primitive = hover.primitive; // TODO currently always 0
    auto&       hover_primitive  = target_mesh->mesh_data.primitives.at(target_primitive);
    hover_primitive.material     = m_material;

    return true;
}

auto Material_paint_tool::on_pick() -> bool
{
    const auto viewport_window = g_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    if (!hover.valid || !hover.mesh)
    {
        return false;
    }
    const auto& target_mesh      = hover.mesh;
    const auto  target_primitive = hover.primitive; // TODO currently always 0
    auto&       hover_primitive  = target_mesh->mesh_data.primitives.at(target_primitive);
    m_material = hover_primitive.material;

    return true;
}

void Material_paint_tool::set_active_command(const int command)
{
    m_active_command = command;

    switch (command)
    {
        case c_command_paint:
        {
            m_paint_command.enable();
            m_pick_command.disable();
            break;
        }
        case c_command_pick:
        {
            m_paint_command.disable();
            m_pick_command.enable();
            break;
        }
        default:
        {
            break;
        }
    }
}

void Material_paint_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority)
    {
        disable();
    }
    if (new_priority > old_priority)
    {
        enable();
    }
}

void Material_paint_tool::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    auto* hover_scene_view = Tool::get_hover_scene_view();
    if (hover_scene_view == nullptr)
    {
        return;
    }

    const auto& scene_root = hover_scene_view->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    const auto& material_library = scene_root->content_library()->materials;
    int command = m_active_command;
    ImGui::RadioButton("Paint", &command, c_command_paint); ImGui::SameLine();
    ImGui::RadioButton("Pick",  &command, c_command_pick);
    if (command != m_active_command)
    {
        set_active_command(command);
    }

    material_library.combo("Material", m_material, false);
#endif
}

} // namespace editor
