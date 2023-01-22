#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"

#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>

namespace editor
{

Editor_scenes* g_editor_scenes{nullptr};

Editor_scenes::Editor_scenes()
    : Component{c_type_name}
{
}

Editor_scenes::~Editor_scenes() noexcept
{
    ERHE_VERIFY(g_editor_scenes == nullptr);
}

void Editor_scenes::deinitialize_component()
{
    ERHE_VERIFY(g_editor_scenes == this);
    m_scene_roots.clear();
    m_current_scene_root.reset();
    g_editor_scenes = nullptr;
}

void Editor_scenes::initialize_component()
{
    ERHE_VERIFY(g_editor_scenes == nullptr);
    g_editor_scenes = this;
}

void Editor_scenes::register_scene_root(
    const std::shared_ptr<Scene_root>& scene_root
)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    m_scene_roots.push_back(scene_root);
}

void Editor_scenes::update_once_per_frame(
    const erhe::components::Time_context&
)
{
    for (const auto& scene_root : m_scene_roots)
    {
        scene_root->scene().update_node_transforms();
    }
}

[[nodiscard]] auto Editor_scenes::get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&
{
    return m_scene_roots;
}

[[nodiscard]] auto Editor_scenes::get_current_scene_root() -> const std::shared_ptr<Scene_root>&
{
    if (m_current_scene_root)
    {
        return m_current_scene_root;
    }
    if (!m_scene_roots.empty())
    {
        return m_scene_roots.front();
    }
    return m_current_scene_root;
}

void Editor_scenes::sanity_check()
{
#if !defined(NDEBUG)
    for (const auto& scene_root : m_scene_roots)
    {
        scene_root->sanity_check();
    }
#endif
}

auto Editor_scenes::scene_combo(
    const char*                  label,
    std::shared_ptr<Scene_root>& in_out_selected_entry,
    const bool                   empty_option
) const -> bool
{
    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<Scene_root>> entries;
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    if (empty_entry)
    {
        names.push_back("(none)");
        entries.push_back({});
        ++index;
    }
    for (const auto& entry : m_scene_roots)
    {
        names.push_back(entry->get_name().c_str());
        entries.push_back(entry);
        if (in_out_selected_entry == entry)
        {
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
    if (selection_changed)
    {
        in_out_selected_entry = entries.at(selection_index);
    }
    return selection_changed;
}

} // namespace hextiles

