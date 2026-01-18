#include "create/create_shape.hpp"

#include "app_context.hpp"
#include "create/create_preview_settings.hpp"
#include "renderers/render_context.hpp"

#include "erhe_renderer/primitive_renderer.hpp"

namespace editor {

Create_shape::~Create_shape() noexcept = default;

auto Create_shape::get_line_renderer(const Create_preview_settings& preview_settings) -> erhe::renderer::Primitive_renderer
{
    erhe::renderer::Debug_renderer_config config {
        .primitive_type    = erhe::graphics::Primitive_type::line,
        .stencil_reference = 2,
        .draw_visible      = true,
        .draw_hidden       = true
    };
    return preview_settings.render_context.app_context.debug_renderer->get(config);
}

}
