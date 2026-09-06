#include "app_scenes.hpp"

#include "app_context.hpp"
#include "assets/asset_manager.hpp"
#include "editor_log.hpp"
#include "app_settings.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "time.hpp"
#include "config/generated/physics_config.hpp"

#include "content_library/content_library.hpp"

#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <utility>

namespace editor {

App_scenes::App_scenes(App_context& context)
    : m_context{context}
{
}

App_scenes::~App_scenes() noexcept
{
    // ~Scene_root calls unregister_from_editor_scenes() which modifies
    // m_scene_roots. Swap to a local so the member is empty before any
    // element destructors run, avoiding mutation of m_scene_roots while it
    // is being torn down.
    std::vector<std::shared_ptr<Scene_root>> roots;
    roots.swap(m_scene_roots);
    // Detach each scene_root from this registry up front. Without this the
    // later ~Scene_root would still believe it is registered and call
    // unregister_scene_root() on the now-empty member, which fails the
    // lookup and logs a spurious "not registered in App_scenes" error.
    for (const std::shared_ptr<Scene_root>& scene_root : roots) {
        scene_root->detach_from_editor_scenes(*this);
    }
}

void App_scenes::register_scene_root(const std::shared_ptr<Scene_root>& scene_root)
{
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

        const auto i = std::find(m_scene_roots.begin(), m_scene_roots.end(), scene_root);
        if (i != m_scene_roots.end()) {
            log_scene->error("Scene '{}' is already in registered in App_scenes", scene_root->get_name());
            return;
        }
        m_scene_roots.push_back(scene_root);
    }

    // R5.3: every registered scene gets an identity container record. The
    // manager pointer is null only outside a full editor run (App_scenes is
    // constructed before Asset_manager; scenes register at runtime, when
    // App_context is populated).
    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->on_scene_registered(scene_root);
    }
}

void App_scenes::unregister_scene_root(Scene_root* scene_root)
{
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

        const auto i = std::remove_if(
            m_scene_roots.begin(), m_scene_roots.end(),
            [scene_root](const std::shared_ptr<Scene_root>& entry) {
                return entry.get() == scene_root;
            }
        );
        if (i == m_scene_roots.end()) {
            log_scene->error("Scene '{}' not registered in App_scenes", scene_root->get_name());
        } else {
            m_scene_roots.erase(i, m_scene_roots.end());
        }
    }

    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->on_scene_unregistered(scene_root);
    }

    // If this scene's own item (issue #240) is currently selected, drop it from
    // the selection now: a torn-down Scene_root would otherwise leave the
    // Selection holding a Scene whose get_item_host() back-pointer dangles.
    const std::shared_ptr<erhe::scene::Scene> scene_item = scene_root->get_scene_item();
    if ((m_context.selection != nullptr) && scene_item && scene_item->is_selected()) {
        m_context.selection->remove_from_selection(scene_item);
    }
}

void App_scenes::notify_scene_source_path_changed(Scene_root& scene_root)
{
    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->on_scene_source_path_changed(scene_root);
    }
}

void App_scenes::imgui()
{
    for (const auto& scene_root : m_scene_roots) {
        scene_root->imgui();
    }
}

void App_scenes::update_physics_simulation_fixed_step(const Time_context& time_context)
{
    ERHE_PROFILE_FUNCTION();

    // Physics enable is resolved per scene (#239): a scene may override it off
    // while others keep simulating, so the gate is inside the per-scene loop.
    for (const auto& scene_root : m_scene_roots) {
        const Physics_config& physics = get_effective_physics(*m_context.editor_settings, *scene_root);
        if (!physics.static_enable || !physics.dynamic_enable) {
            continue;
        }
        scene_root->update_physics_simulation_fixed_step(time_context.simulation_dt_s, physics);
    }
}

void App_scenes::before_physics_simulation_steps()
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& scene_root : m_scene_roots) {
        const Physics_config& physics = get_effective_physics(*m_context.editor_settings, *scene_root);
        const bool running = physics.static_enable && physics.dynamic_enable;
        // Edge-triggered: clears no_transform_update from body-driven nodes
        // on pause and restores it on resume, so paused bodies follow
        // hierarchy edits again (the flag otherwise sticks - no deactivation
        // events fire while the simulation is not stepping).
        scene_root->set_physics_simulation_running(running);
        if (!running) {
            continue;
        }
        scene_root->before_physics_simulation_steps();
    }
}

void App_scenes::flush_draw_lists()
{
    ERHE_PROFILE_FUNCTION();

    // Copy under m_mutex, flush outside it: flush_draw_lists() takes the
    // scene root's item_host_mutex, and m_mutex must never be held while
    // waiting on that (is_host_registered() is called from item-host code).
    std::vector<std::shared_ptr<Scene_root>> scene_roots;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        scene_roots = m_scene_roots;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
        scene_root->flush_draw_lists();
    }
}

void App_scenes::update_material_sets(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    // Copy under m_mutex for the same documented reason flush_draw_lists()
    // does: the work below takes scene-root locks, and m_mutex must never be
    // held while waiting on those.
    std::vector<std::shared_ptr<Scene_root>> scene_roots;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        scene_roots = m_scene_roots;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
        const std::shared_ptr<Content_library>& content_library = scene_root->get_content_library();
        if (!content_library) {
            continue;
        }
        const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials =
            content_library->get_all<erhe::primitive::Material>();
        const std::span<const std::shared_ptr<erhe::primitive::Material>> material_span{materials};

        // A root with a draw list carries TWO sets, and the two reconcile the
        // same library independently: a material normally holds a different
        // slot in each, and each render path resolves through the set it
        // binds.
        erhe::scene_renderer::Material_set& forward_set = scene_root->get_material_set();
        forward_set.sync_library(material_span);
        forward_set.flush_pending();
        if (forward_set.has_gpu()) {
            forward_set.update(command_buffer);
        }

        erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
        if (draw_list_scene != nullptr) {
            erhe::scene_renderer::Material_set& draw_list_set = draw_list_scene->get_material_set();
            draw_list_set.sync_library(material_span);
            if (draw_list_set.has_gpu()) {
                draw_list_set.update(command_buffer);
            }
        }
    }
}

void App_scenes::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION();

    // Scene::update_node_transforms() locks the scene's Item_host mutex
    // itself, synchronizing against async workers.
    for (const auto& scene_root : m_scene_roots) {
        scene_root->get_scene().update_node_transforms();
    }
}

void App_scenes::update_layout_nodes()
{
    ERHE_PROFILE_FUNCTION();

    // Each Scene keeps its registered Layout attachments (Scene_host
    // register_layout / unregister_layout hooks), so this touches only the
    // layout nodes - no per-frame hierarchy scan.
    for (const std::shared_ptr<Scene_root>& scene_root : m_scene_roots) {
        scene_root->get_scene().update_layouts();
    }
}

void App_scenes::after_physics_simulation_steps()
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& scene_root : m_scene_roots) {
        const Physics_config& physics = get_effective_physics(*m_context.editor_settings, *scene_root);
        if (!physics.static_enable || !physics.dynamic_enable) {
            continue;
        }
        scene_root->after_physics_simulation_steps();
    }
}

auto App_scenes::get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&
{
    return m_scene_roots;
}

auto App_scenes::is_host_registered(const erhe::Item_host* item_host) -> bool
{
    if (item_host == nullptr) {
        return false;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    for (const std::shared_ptr<Scene_root>& scene_root : m_scene_roots) {
        if (static_cast<const erhe::Item_host*>(scene_root.get()) == item_host) {
            return true;
        }
    }
    return false;
}

auto App_scenes::get_single_scene_root() -> std::shared_ptr<Scene_root>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    if (m_scene_roots.size() == 1) {
        return m_scene_roots.front();
    }
    return {};
}

void App_scenes::sanity_check()
{
#if !defined(NDEBUG)
    for (const auto& scene_root : m_scene_roots) {
        scene_root->sanity_check();
    }
#endif
}

auto App_scenes::scene_combo(const char* label, std::shared_ptr<Scene_root>& in_out_selected_entry, const bool empty_option) const -> bool
{
    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<Scene_root>> entries;
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

} // namespace editor
