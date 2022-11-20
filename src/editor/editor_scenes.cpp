#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"
#include "erhe/scene/scene.hpp"

namespace editor
{

Editor_scenes::Editor_scenes()
    : Component{c_type_name}
{
}

Editor_scenes::~Editor_scenes() noexcept
{
}

void Editor_scenes::register_scene_root(const std::shared_ptr<Scene_root>& scene_root)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    m_scene_roots.push_back(scene_root);
}

void Editor_scenes::update_once_per_frame(const erhe::components::Time_context&)
{
    m_message_bus.notify();
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

[[nodiscard]] auto Editor_scenes::get_message_bus() -> erhe::scene::Message_bus*
{
    return &m_message_bus;
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

} // namespace hextiles

