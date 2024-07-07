#include "tools/brushes/create/create_shape.hpp"

#include "renderers/render_context.hpp"
#include "tools/brushes/create/create_preview_settings.hpp"

#include "erhe_renderer/line_renderer.hpp"

#include "editor_context.hpp"

namespace editor {

auto Create_shape::get_line_renderer(const Create_preview_settings& preview_settings) -> erhe::renderer::Line_renderer&
{
    return *preview_settings.render_context.editor_context.line_renderer_set->hidden.at(2).get();
}

}
