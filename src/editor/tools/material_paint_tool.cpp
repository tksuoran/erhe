#include "tools/material_paint_tool.hpp"
#include "log.hpp"
#include "editor_rendering.hpp"

#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "tools/tools.hpp"
#include "windows/operations.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/scene/mesh.hpp"

#include <imgui.h>

namespace editor
{

void Material_paint_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (m_material_paint_tool.on_paint_ready())
    {
        set_ready(context);
    }
}

auto Material_paint_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        m_material_paint_tool.on_paint() &&
        (state() == erhe::application::State::Ready)
    )
    {
        set_active(context);
    }

    return state() == erhe::application::State::Active;
}

////////

void Material_pick_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (m_material_paint_tool.on_pick_ready())
    {
        set_ready(context);
    }
}

auto Material_pick_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        m_material_paint_tool.on_pick() &&
        (state() == erhe::application::State::Ready)
    )
    {
        set_active(context);
    }

    return state() == erhe::application::State::Active;
}


/////////

Material_paint_tool::Material_paint_tool()
    : erhe::components::Component{c_label}
    , m_paint_command            {*this}
    , m_pick_command             {*this}
{
}

void Material_paint_tool::declare_required_components()
{
    require<Tools                  >();
    require<erhe::application::View>();
    require<Operations             >();
}

void Material_paint_tool::initialize_component()
{
    get<Tools>()->register_tool(this);

    const auto view = get<erhe::application::View>();
    view->register_command(&m_paint_command);
    view->register_command(&m_pick_command);
    view->bind_command_to_mouse_click(&m_paint_command, erhe::toolkit::Mouse_button_right);
    view->bind_command_to_mouse_click(&m_pick_command,  erhe::toolkit::Mouse_button_right);

    erhe::application::Command_context context
    {
        *view.get()
    };
    set_active_command(c_command_paint);

    get<Operations>()->register_active_tool(this);
}

void Material_paint_tool::post_initialize()
{
    m_pointer_context = get<Pointer_context>();
    m_scene_root      = get<Scene_root>();
}

auto Material_paint_tool::description() -> const char*
{
    return c_title.data();
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
    erhe::application::Command_context context
    {
        *get<erhe::application::View>().get()
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

} // namespace editor
