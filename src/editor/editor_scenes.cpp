#include "editor_scenes.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_settings.hpp"
#include "tools/tools.hpp"
#include "scene/scene_root.hpp"

#include "erhe/physics/iworld.hpp"
#include "erhe/scene/scene.hpp"

#include <imgui.h>

namespace editor
{

Editor_scenes::Editor_scenes(
    Editor_context& editor_context,
    Time&           time
)
    : Update_time_base{time}
    , m_context       {editor_context}
{
}

void Editor_scenes::register_scene_root(
    Scene_root* scene_root
)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    const auto i = std::find(m_scene_roots.begin(), m_scene_roots.end(), scene_root);
    if (i != m_scene_roots.end()) {
        log_scene->error("Scene '{}' is already in registered in Editor_scenes", scene_root->get_name());
    } else {
        m_scene_roots.push_back(scene_root);
    }
}

void Editor_scenes::unregister_scene_root(
    Scene_root* scene_root
)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    const auto i = std::remove(m_scene_roots.begin(), m_scene_roots.end(), scene_root);
    if (i == m_scene_roots.end()) {
        log_scene->error("Scene '{}' not registered in Editor_scenes", scene_root->get_name());
    } else {
        m_scene_roots.erase(i, m_scene_roots.end());
    }
}

void Editor_scenes::update_physics_simulation_fixed_step(const Time_context& time_context)
{
    if (
        !m_context.editor_settings->physics_static_enable ||
        !m_context.editor_settings->physics_dynamic_enable
    ) {
        return;
    }

    for (const auto& scene_root : m_scene_roots) {
        scene_root->update_physics_simulation_fixed_step(time_context.dt);
    }
}

void Editor_scenes::update_physics_simulation_once_per_frame()
{
    if (
        !m_context.editor_settings->physics_static_enable ||
        !m_context.editor_settings->physics_dynamic_enable
    ) {
        return;
    }

    for (const auto& scene_root : m_scene_roots) {
        scene_root->update_physics_simulation_once_per_frame();
    }
}

void Editor_scenes::update_node_transforms()
{
    for (const auto& scene_root : m_scene_roots) {
        scene_root->get_scene().update_node_transforms();
    }

    // Not in m_scene_roots
    m_context.tools->get_tool_scene_root()->get_hosted_scene()->update_node_transforms();
}

void Editor_scenes::update_fixed_step(const Time_context& time_context)
{
    update_physics_simulation_fixed_step(time_context);
}

void Editor_scenes::update_once_per_frame(const Time_context&)
{
    update_physics_simulation_once_per_frame();
    update_node_transforms();
}

[[nodiscard]] auto Editor_scenes::get_scene_roots() -> const std::vector<Scene_root*>&
{
    return m_scene_roots;
}

void Editor_scenes::sanity_check()
{
#if !defined(NDEBUG)
    for (const auto& scene_root : m_scene_roots) {
        scene_root->sanity_check();
    }
#endif
}

auto Editor_scenes::scene_combo(
    const char*  label,
    Scene_root*& in_out_selected_entry,
    const bool   empty_option
) const -> bool
{
    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<Scene_root*> entries;
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    if (empty_entry) {
        names.push_back("(none)");
        entries.push_back({});
        ++index;
    }
    for (const auto& entry : m_scene_roots) {
        names.push_back(entry->get_name().c_str());
        entries.push_back(entry);
        if (in_out_selected_entry == entry) {
            selection_index = index;
        }
        ++index;
    }

    // TODO Move to begin / end combo
    const bool selection_changed =
        ImGui::Combo(
            label,
            &selection_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (in_out_selected_entry != entries.at(selection_index));
    if (selection_changed) {
        in_out_selected_entry = entries.at(selection_index);
    }
    return selection_changed;
}

} // namespace hextiles

