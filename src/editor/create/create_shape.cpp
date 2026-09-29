#include "create/create_shape.hpp"

#include "app_context.hpp"
#include "create/create_preview_settings.hpp"
#include "renderers/render_context.hpp"

#include "erhe_renderer/primitive_renderer.hpp"

namespace editor {

Create_shape::~Create_shape() noexcept = default;

auto Create_shape::get_line_renderer(const Create_preview_settings& preview_settings) -> erhe::renderer::Primitive_renderer
{
    // Two buckets: the visible (major-style) lines at stencil reference 2
    // and the self-occluded (minor-style) lines one below, so a visible
    // segment wins every pixel it shares with a self-occluded one whatever
    // order the shape helper emits them in (see
    // Primitive_renderer::set_minor_line_renderer).
    erhe::renderer::Debug_renderer& debug_renderer = *preview_settings.render_context.app_context.debug_renderer;
    const erhe::renderer::Debug_renderer_config minor_config{
        .primitive_type    = erhe::graphics::Primitive_type::line,
        .stencil_reference = 1,
        .draw_visible      = true,
        .draw_hidden       = preview_settings.draw_hidden
    };
    const erhe::renderer::Debug_renderer_config major_config{
        .primitive_type    = erhe::graphics::Primitive_type::line,
        .stencil_reference = 2,
        .draw_visible      = true,
        .draw_hidden       = preview_settings.draw_hidden
    };
    const erhe::renderer::Primitive_renderer minor_line_renderer = debug_renderer.get(minor_config);
    erhe::renderer::Primitive_renderer       line_renderer       = debug_renderer.get(major_config);
    line_renderer.set_minor_line_renderer(minor_line_renderer);
    line_renderer.set_minor_lines(
        preview_settings.self_occluded_lines ? erhe::renderer::Minor_lines::draw : erhe::renderer::Minor_lines::skip
    );
    return line_renderer;
}

}
