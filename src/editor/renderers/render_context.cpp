#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"

#include "erhe/scene/node.hpp"


namespace editor
{

auto Render_context::get_camera_node() const  -> const erhe::scene::Node*
{
    if (scene_view == nullptr)
    {
        return nullptr;
    }
    const auto& view_camera = scene_view->get_camera();
    if (!view_camera)
    {
        return nullptr;
    }
    return view_camera->get_node();
}

auto Render_context::get_scene() const -> const erhe::scene::Scene*
{
    const auto* camera_node = get_camera_node();
    if (camera_node == nullptr)
    {
        return nullptr;
    }
    return camera_node->get_scene();
}

} // namespace editor
