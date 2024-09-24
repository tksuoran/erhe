#include "tools/brushes/create/create_shape.hpp"

#include "renderers/render_context.hpp"
#include "tools/brushes/create/create_preview_settings.hpp"

#include "erhe_renderer/scoped_line_renderer.hpp"

#include "editor_context.hpp"

namespace editor {

auto Create_shape::get_line_renderer(const Create_preview_settings& preview_settings) -> erhe::renderer::Scoped_line_renderer
{
    erhe::renderer::Line_renderer_config config {
        .stencil_reference = 2,
        .draw_visible      = true,
        .draw_hidden       = true,
        .reverse_depth     = true
    };
    return preview_settings.render_context.editor_context.line_renderer->get(config);
}

}
