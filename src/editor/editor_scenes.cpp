#include "editor_scenes.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_settings.hpp"
#include "tools/tools.hpp"
#include "scene/scene_root.hpp"

#include "erhe_physics/iworld.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/scene.hpp"

#include <imgui/imgui.h>

namespace editor {

Editor_scenes::Editor_scenes(Editor_context& editor_context)
    : m_context{editor_context}
{
}

void Editor_scenes::register_scene_root(Scene_root* scene_root)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto i = std::find(m_scene_roots.begin(), m_scene_roots.end(), scene_root);
    if (i != m_scene_roots.end()) {
        log_scene->error("Scene '{}' is already in registered in Editor_scenes", scene_root->get_name());
    } else {
        m_scene_roots.push_back(scene_root);
    }
}

void Editor_scenes::unregister_scene_root(Scene_root* scene_root)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto i = std::remove(m_scene_roots.begin(), m_scene_roots.end(), scene_root);
    if (i == m_scene_roots.end()) {
        log_scene->error("Scene '{}' not registered in Editor_scenes", scene_root->get_name());
    } else {
        m_scene_roots.erase(i, m_scene_roots.end());
    }
}

void Editor_scenes::imgui()
{
    for (const auto& scene_root : m_scene_roots) {
        scene_root->imgui();
    }
}

void Editor_scenes::update_physics_simulation_fixed_step(const Time_context& time_context)
{
    ERHE_PROFILE_FUNCTION();

    if (
        !m_context.editor_settings->physics.static_enable ||
        !m_context.editor_settings->physics.dynamic_enable
    ) {
        return;
    }

    for (const auto& scene_root : m_scene_roots) {
        scene_root->update_physics_simulation_fixed_step(time_context.simulation_dt_s);
    }
}

void Editor_scenes::before_physics_simulation_steps()
{
    ERHE_PROFILE_FUNCTION();

    if (!m_context.editor_settings->physics.static_enable || !m_context.editor_settings->physics.dynamic_enable) {
        return;
    }

    for (const auto& scene_root : m_scene_roots) {
        scene_root->before_physics_simulation_steps();
    }
}

void Editor_scenes::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& scene_root : m_scene_roots) {
        // TODO ? std::lock_guard<std::mutex> scene_lock{scene_root->item_host_mutex};
        scene_root->get_scene().update_node_transforms();
    }

    // Not in m_scene_roots
    Scene_root& scene_root = *m_context.tools->get_tool_scene_root().get();
    // TODO ? std::lock_guard<std::mutex> scene_lock{scene_root.item_host_mutex};
    scene_root.get_hosted_scene()->update_node_transforms();
}

void Editor_scenes::after_physics_simulation_steps()
{
    ERHE_PROFILE_FUNCTION();

    if (!m_context.editor_settings->physics.static_enable || !m_context.editor_settings->physics.dynamic_enable) {
        return;
    }

    for (const auto& scene_root : m_scene_roots) {
        scene_root->after_physics_simulation_steps();
    }
}

auto Editor_scenes::get_scene_roots() -> const std::vector<Scene_root*>&
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

auto Editor_scenes::scene_combo(const char* label, Scene_root*& in_out_selected_entry, const bool empty_option) const -> bool
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

