#include "app_scenes.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_settings.hpp"
#include "tools/tools.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "config/generated/physics_config.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

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
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto i = std::find(m_scene_roots.begin(), m_scene_roots.end(), scene_root);
    if (i != m_scene_roots.end()) {
        log_scene->error("Scene '{}' is already in registered in App_scenes", scene_root->get_name());
    } else {
        m_scene_roots.push_back(scene_root);
    }
}

void App_scenes::unregister_scene_root(Scene_root* scene_root)
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

    // If this scene's own item (issue #240) is currently selected, drop it from
    // the selection now: a torn-down Scene_root would otherwise leave the
    // Selection holding a Scene whose get_item_host() back-pointer dangles.
    const std::shared_ptr<erhe::scene::Scene> scene_item = scene_root->get_scene_item();
    if ((m_context.selection != nullptr) && scene_item && scene_item->is_selected()) {
        m_context.selection->remove_from_selection(scene_item);
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
        scene_root->update_physics_simulation_fixed_step(time_context.simulation_dt_s);
    }
}

void App_scenes::before_physics_simulation_steps()
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& scene_root : m_scene_roots) {
        const Physics_config& physics = get_effective_physics(*m_context.editor_settings, *scene_root);
        if (!physics.static_enable || !physics.dynamic_enable) {
            continue;
        }
        scene_root->before_physics_simulation_steps();
    }
}

void App_scenes::update_node_transforms()
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

void App_scenes::update_layout_nodes()
{
    ERHE_PROFILE_FUNCTION();

    for (const std::shared_ptr<Scene_root>& scene_root : m_scene_roots) {
        const std::shared_ptr<erhe::scene::Node> root = scene_root->get_scene().get_root_node();
        if (!root) {
            continue;
        }

        // Collect layout nodes together with their hierarchy depth.
        std::vector<std::pair<std::size_t, erhe::scene::Node*>> layout_nodes;
        root->for_each<erhe::scene::Node>(
            [&layout_nodes](erhe::scene::Node& node) -> bool {
                const std::shared_ptr<erhe::scene::Layout> layout = erhe::scene::get_attachment<erhe::scene::Layout>(&node);
                if (layout) {
                    layout_nodes.emplace_back(node.get_depth(), &node);
                }
                return true;
            }
        );

        // Shallow-to-deep so a parent layout runs before any nested child layout;
        // a nested layout then re-runs later in this same pass.
        std::stable_sort(
            layout_nodes.begin(),
            layout_nodes.end(),
            [](const std::pair<std::size_t, erhe::scene::Node*>& lhs, const std::pair<std::size_t, erhe::scene::Node*>& rhs) -> bool {
                return lhs.first < rhs.first;
            }
        );

        for (const std::pair<std::size_t, erhe::scene::Node*>& entry : layout_nodes) {
            const std::shared_ptr<erhe::scene::Layout> layout = erhe::scene::get_attachment<erhe::scene::Layout>(entry.second);
            layout->update();
        }
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
