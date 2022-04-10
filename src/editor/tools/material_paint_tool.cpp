#include "tools/material_paint_tool.hpp"
#include "tools/material_paint_tool.hpp"
#include "editor_imgui_windows.hpp"
#include "editor_tools.hpp"
#include "editor_view.hpp"
#include "log.hpp"
#include "rendering.hpp"

#include "commands/command_context.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "windows/operations.hpp"

#include "erhe/scene/mesh.hpp"

#include <imgui.h>

namespace editor
{

void Material_paint_command::try_ready(Command_context& context)
{
    if (state() != State::Inactive)
    {
        return;
    }

    if (m_material_paint_tool.on_paint_ready())
    {
        set_ready(context);
    }
}

auto Material_paint_command::try_call(Command_context& context) -> bool
{
    if (state() == State::Inactive)
    {
        return false;
    }

    if (
        m_material_paint_tool.on_paint() &&
        (state() == State::Ready)
    )
    {
        set_active(context);
    }

    return state() == State::Active;
}

////////

void Material_pick_command::try_ready(Command_context& context)
{
    if (state() != State::Inactive)
    {
        return;
    }

    if (m_material_paint_tool.on_pick_ready())
    {
        set_ready(context);
    }
}

auto Material_pick_command::try_call(Command_context& context) -> bool
{
    if (state() == State::Inactive)
    {
        return false;
    }

    if (
        m_material_paint_tool.on_pick() &&
        (state() == State::Ready)
    )
    {
        set_active(context);
    }

    return state() == State::Active;
}


/////////

Material_paint_tool::Material_paint_tool()
    : erhe::components::Component{c_name}
    , m_paint_command{*this}
    , m_pick_command {*this}
{
}

void Material_paint_tool::connect()
{
    m_pointer_context = get<Pointer_context>();
    m_scene_root      = get<Scene_root>();
    require<Editor_tools>();
    require<Editor_view >();
    require<Operations  >();
}

void Material_paint_tool::initialize_component()
{
    get<Editor_tools>()->register_tool(this);

    const auto view = get<Editor_view>();
    view->register_command(&m_paint_command);
    view->register_command(&m_pick_command);
    view->bind_command_to_mouse_click(&m_paint_command, erhe::toolkit::Mouse_button_right);
    view->bind_command_to_mouse_click(&m_pick_command,  erhe::toolkit::Mouse_button_right);

    Command_context context
    {
        *view.get()
    };
    set_active_command(c_command_paint);

    get<Operations>()->register_active_tool(this);
}

auto Material_paint_tool::description() -> const char*
{
    return c_description.data();
}


auto Material_paint_tool::on_paint_ready() -> bool
{
    if (!is_enabled())
    {
        return false;
    }

    const auto& hover = m_pointer_context->get_hover(Pointer_context::content_slot);
    return hover.valid && hover.mesh;
}

auto Material_paint_tool::on_pick_ready() -> bool
{
    if (!is_enabled())
    {
        return false;
    }

    const auto& hover = m_pointer_context->get_hover(Pointer_context::content_slot);
    return hover.valid && hover.mesh;
}

auto Material_paint_tool::on_paint() -> bool
{
    if (!is_enabled())
    {
        return false;
    }

    const auto& hover = m_pointer_context->get_hover(Pointer_context::content_slot);
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
    if (!is_enabled())
    {
        return false;
    }

    const auto& hover = m_pointer_context->get_hover(Pointer_context::content_slot);
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
    Command_context context
    {
        *get<Editor_view>().get()
    };

    switch (command)
    {
        case c_command_paint:
        {
            m_paint_command.enable(context);
            m_pick_command.disable(context);
            break;
        }
        case c_command_pick:
        {
            m_paint_command.disable(context);
            m_pick_command.enable(context);
            break;
        }
        default:
        {
            break;
        }
    }
}

void Material_paint_tool::tool_properties()
{
    int command = m_active_command;
    ImGui::RadioButton("Paint", &command, c_command_paint); ImGui::SameLine();
    ImGui::RadioButton("Pick",  &command, c_command_pick);
    if (command != m_active_command)
    {
        set_active_command(command);
    }

    m_scene_root->material_combo("Material", m_material);
}

}
