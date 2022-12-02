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

} // namespace editor
