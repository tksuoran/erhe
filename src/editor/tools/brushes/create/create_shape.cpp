#include "tools/brushes/create/create_shape.hpp"

#include "renderers/render_context.hpp"
#include "tools/brushes/create/create_preview_settings.hpp"

#include "erhe_renderer/primitive_renderer.hpp"

#include "editor_context.hpp"

namespace editor {

auto Create_shape::get_line_renderer(const Create_preview_settings& preview_settings) -> erhe::renderer::Primitive_renderer
{
    erhe::renderer::Debug_renderer_config config {
        .primitive_type    = gl::Primitive_type::lines,
        .stencil_reference = 2,
        .draw_visible      = true,
        .draw_hidden       = true
    };
    return preview_settings.render_context.editor_context.debug_renderer->get(config);
}

}
