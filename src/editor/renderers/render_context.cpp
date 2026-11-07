#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "app_context.hpp"
#include "editor_item_bits.hpp"

#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

auto c_str(const Mesh_form form) -> const char*
{
    switch (form) {
        case Mesh_form::source:      return "source";
        case Mesh_form::world_space: return "world_space";
        default:                     return "?";
    }
}

auto get_view_filter(const erhe::Item_filter& filter, const Mesh_form mesh_form) -> erhe::Item_filter
{
    erhe::Item_filter view_filter = filter;
    view_filter.require_all_bits_clear |= (mesh_form == Mesh_form::world_space)
        ? Editor_item_flags::world_space_source
        : Editor_item_flags::world_space_form;
    return view_filter;
}

auto Render_context::get_camera_node() const -> const erhe::scene::Node*
{
    const auto& view_camera = scene_view.get_camera();
    if (!view_camera) {
        return nullptr;
    }
    return view_camera.get();
}

auto Render_context::get_scene() const -> const erhe::scene::Scene*
{
    const auto* camera_node = get_camera_node();
    if (camera_node == nullptr) {
        return nullptr;
    }
    return camera_node->get_scene();
}

auto Render_context::get(const erhe::renderer::Debug_renderer_config& config) const -> erhe::renderer::Primitive_renderer
{
    return app_context.debug_renderer->get(config);
}

}
