#include "tools/material_paint_tool.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"

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

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

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
    : erhe::components::Component{c_type_name}
    , m_paint_command            {*this}
    , m_pick_command             {*this}
{
}

void Material_paint_tool::declare_required_components()
{
    require<erhe::application::Commands>();
    require<Operations>();
    require<Tools     >();
}

void Material_paint_tool::initialize_component()
{
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

    get<Operations>()->register_active_tool(this);
}

void Material_paint_tool::post_initialize()
{
    m_editor_scenes    = get<Editor_scenes   >();
    m_viewport_windows = get<Viewport_windows>();
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
    if (!is_enabled())
    {
        return false;
    }

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
    if (!is_enabled())
    {
        return false;
    }

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
    if (!is_enabled())
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
    const auto& scene_root = m_editor_scenes->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    const auto& material_library = scene_root->material_library();
    if (!material_library)
    {
        return;
    }

    int command = m_active_command;
    ImGui::RadioButton("Paint", &command, c_command_paint); ImGui::SameLine();
    ImGui::RadioButton("Pick",  &command, c_command_pick);
    if (command != m_active_command)
    {
        set_active_command(command);
    }

    material_library->material_combo("Material", m_material);
#endif
}

} // namespace editor
