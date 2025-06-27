#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "editor_context.hpp"

#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

auto Render_context::get_camera_node() const -> const erhe::scene::Node*
{
    const auto& view_camera = scene_view.get_camera();
    if (!view_camera) {
        return nullptr;
    }
    return view_camera->get_node();
}

auto Render_context::get_scene() const -> const erhe::scene::Scene*
{
    const auto* camera_node = get_camera_node();
    if (camera_node == nullptr) {
        return nullptr;
    }
    return camera_node->get_scene();
}

auto Render_context::get_line_renderer(const erhe::renderer::Debug_renderer_config& config) const -> erhe::renderer::Primitive_renderer
{
    return editor_context.debug_renderer->get(config);
}

auto Render_context::get_line_renderer(unsigned int stencil, bool visible, bool hidden) const -> erhe::renderer::Primitive_renderer
{
    erhe::renderer::Debug_renderer_config config{
        .primitive_type    = gl::Primitive_type::lines,
        .stencil_reference = stencil,
        .draw_visible      = visible,
        .draw_hidden       = hidden
    };
    return editor_context.debug_renderer->get(config);
}

} // namespace editor
