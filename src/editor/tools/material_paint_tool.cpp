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
#include "erhe/application/view.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

void Material_paint_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (get_command_state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (m_material_paint_tool.on_paint_ready())
    {
        set_ready(context);
    }
}

Material_paint_command::Material_paint_command(Material_paint_tool& material_paint_tool)
    : Command              {"Material_paint.paint"}
    , m_material_paint_tool{material_paint_tool}
{
    set_host(&material_paint_tool);
}

auto Material_paint_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (get_command_state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        m_material_paint_tool.on_paint() &&
        (get_command_state() == erhe::application::State::Ready)
    )
    {
        set_active(context);
    }

    return get_command_state() == erhe::application::State::Active;
}

////////

void Material_pick_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (get_command_state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (m_material_paint_tool.on_pick_ready())
    {
        set_ready(context);
    }
}

Material_pick_command::Material_pick_command(Material_paint_tool& material_paint_tool)
    : Command              {"Material_paint.pick"}
    , m_material_paint_tool{material_paint_tool}
{
    set_host(&material_paint_tool);
}

auto Material_pick_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (get_command_state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        m_material_paint_tool.on_pick() &&
        (get_command_state() == erhe::application::State::Ready)
    )
    {
        set_active(context);
    }

    return get_command_state() == erhe::application::State::Active;
}


/////////

Material_paint_tool::Material_paint_tool()
    : erhe::components::Component{c_type_name}
    , m_paint_command            {*this}
    , m_pick_command             {*this}
{
}

void Material_paint_tool::declare_required_components()
{
    require<erhe::application::Commands>();
    require<Icon_set  >();
    require<Operations>();
    require<Tools     >();
}

void Material_paint_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox);
    set_icon         (get<Icon_set>()->icons.material);
    get<Tools>()->register_tool(this);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_paint_command);
    commands->register_command(&m_pick_command);
    commands->bind_command_to_mouse_click(&m_paint_command, erhe::toolkit::Mouse_button_right);
    commands->bind_command_to_mouse_click(&m_pick_command,  erhe::toolkit::Mouse_button_right);

    erhe::application::Command_context context
    {
        *commands.get()
    };
    set_active_command(c_command_paint);
}

void Material_paint_tool::post_initialize()
{
    m_editor_scenes    = get<Editor_scenes   >();
    m_viewport_windows = get<Viewport_windows>();
}

auto Material_paint_tool::on_paint_ready() -> bool
{
    const auto viewport_window = m_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return false;
    }
    const Hover_entry& hover = viewport_window->get_hover(Hover_entry::content_slot);
    return hover.valid && hover.mesh;
}

auto Material_paint_tool::on_pick_ready() -> bool
{
    const auto viewport_window = m_viewport_windows->hover_window();
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

    const auto viewport_window = m_viewport_windows->hover_window();
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
    const auto viewport_window = m_viewport_windows->hover_window();
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
    erhe::application::Command_context context
    {
        *get<erhe::application::Commands>().get()
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
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto& scene_root = m_editor_scenes->get_current_scene_root();
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
