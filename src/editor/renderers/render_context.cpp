#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "editor_context.hpp"

#include "erhe_renderer/scoped_line_renderer.hpp"
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

auto Render_context::get_line_renderer(const erhe::renderer::Line_renderer_config& config) const -> erhe::renderer::Scoped_line_renderer
{
    return editor_context.line_renderer->get(config);
}

auto Render_context::get_line_renderer(unsigned int stencil, bool visible, bool hidden, bool indirect) const -> erhe::renderer::Scoped_line_renderer
{
    erhe::renderer::Line_renderer_config config{
        .stencil_reference = stencil,
        .draw_visible      = visible,
        .draw_hidden       = hidden,
        .reverse_depth     = true,
        .indirect          = indirect
    };
    return editor_context.line_renderer->get(config);
}

auto Render_context::get_line_renderer_indirect(unsigned int stencil, bool visible, bool hidden) const -> erhe::renderer::Scoped_line_renderer
{
    return get_line_renderer(stencil, visible, hidden, true);
}

} // namespace editor
