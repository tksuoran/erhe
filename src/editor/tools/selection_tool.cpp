#include "tools/selection_tool.hpp"
#include "tools/pointer_context.hpp"
#include "tools/trs_tool.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/scene/mesh.hpp"
#include "log.hpp"

#include "imgui.h"

namespace editor
{

using namespace erhe::toolkit;

auto Selection_tool::state() const -> Tool::State
{
    return m_state;
}

Selection_tool::Subcription Selection_tool::subscribe_mesh_selection_change_notification(On_mesh_selection_changed callback)
{
    std::lock_guard lock(m_mutex);
    int handle = m_next_mesh_selection_change_subscription++;
    m_mesh_selection_change_subscriptions.push_back({callback, handle});
    return Subcription(this, handle);
}

void Selection_tool::unsubscribe_mesh_selection_change_notification(int handle)
{
    std::lock_guard lock(m_mutex);
    m_mesh_selection_change_subscriptions.erase(std::remove_if(m_mesh_selection_change_subscriptions.begin(),
                                                               m_mesh_selection_change_subscriptions.end(),
                                                               [=](Subscription_entry& entry) -> bool
                                                               {
                                                                   return entry.handle == handle;
                                                               }),
                                                m_mesh_selection_change_subscriptions.end());
}

void Selection_tool::connect()
{
    m_scene_manager = get<Scene_manager>();
}

void Selection_tool::window(Pointer_context&)
{
    ImGui::Begin("Selection");
    for (const auto& mesh : m_selected_meshes)
    {
        if (!mesh)
        {
            continue;
        }
        ImGui::Text("Mesh: %s", mesh->name.c_str());
        auto* node = mesh->node.get();
        if (node != nullptr)
        {
            ImGui::Text("Node: %s", node->name.c_str());
        }
        ImGui::Separator();
    }
    ImGui::End();
}

void Selection_tool::cancel_ready()
{
    m_state = State::passive;
}

auto Selection_tool::update(Pointer_context& pointer_context) -> bool
{
    if (pointer_context.priority_action != Action::select)
    {
        if (m_state != State::passive)
        {
            cancel_ready();
        }
        return false;
    }

    if (!pointer_context.scene_view_focus ||
        !pointer_context.pointer_in_content_area())
    {
        return false;
    }
    m_hover_mesh     = pointer_context.hover_mesh;
    m_hover_position = glm::vec3(pointer_context.pointer_x,
                                 pointer_context.pointer_y,
                                 pointer_context.pointer_z);
    m_hover_content  = pointer_context.hover_content;
    m_hover_tool     = pointer_context.hover_tool;
    
    if (m_state == State::passive)
    {
        if (m_hover_content && pointer_context.mouse_button[Mouse_button_left].pressed)
        {
            m_state = State::ready;
            return true;
        }
        return false;
    }

    if (m_state == State::passive)
    {
        return false;
    }

    if (pointer_context.mouse_button[Mouse_button_left].released)
    {
        if (pointer_context.shift)
        {
            if (!m_hover_content)
            {
                return false;
            }
            toggle_mesh_selection(m_hover_mesh, false);
            m_state = State::passive;
            return true;
        }
        if (!m_hover_content)
        {
            clear_mesh_selection();
            m_state = State::passive;
            return true;
        }
        toggle_mesh_selection(m_hover_mesh, true);
        m_state = State::passive;
        return true;
    }
    return false;
}

void Selection_tool::toggle_mesh_selection(std::shared_ptr<erhe::scene::Mesh> mesh, bool clear_others)
{
    if (clear_others)
    {
        auto i = std::find(m_selected_meshes.begin(),
                           m_selected_meshes.end(),
                           mesh);
        bool was_selected = i != m_selected_meshes.end();
        m_selected_meshes.clear();
        if (!was_selected && mesh)
        {
            m_selected_meshes.push_back(mesh);
        }
    }
    else if (mesh)
    {
        auto i = std::remove(m_selected_meshes.begin(),
                             m_selected_meshes.end(),
                             mesh);
        if (i != m_selected_meshes.end())
        {
            m_selected_meshes.erase(i, m_selected_meshes.end());
        }
        else
        {
            m_selected_meshes.push_back(mesh);
        }
    }

    call_mesh_selection_change_subscriptions();
}

void Selection_tool::call_mesh_selection_change_subscriptions()
{
    for (const auto& entry : m_mesh_selection_change_subscriptions)
    {
        entry.callback(m_selected_meshes);
    }
}

void Selection_tool::clear_mesh_selection()
{
    std::lock_guard lock(m_mutex);
    m_selected_meshes.clear();
    call_mesh_selection_change_subscriptions();
}

void Selection_tool::render(Render_context& render_context)
{
    static_cast<void>(render_context);
}

}
