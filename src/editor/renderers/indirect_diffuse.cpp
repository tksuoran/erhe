#include "renderers/indirect_diffuse.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "renderers/ddgi_renderer.hpp"
#include "renderers/radiance_cascades_renderer.hpp"

namespace editor {

auto get_producer_selection(const Indirect_diffuse_source current, const Indirect_diffuse_source producer) -> Producer_selection
{
    return (current == producer) ? Producer_selection::selected : Producer_selection::deselected;
}

void set_indirect_diffuse_source(App_context& context, const Indirect_diffuse_source source)
{
    if (context.editor_settings != nullptr) {
        context.editor_settings->indirect_diffuse_source = source;
    }
    if (context.ddgi_renderer != nullptr) {
        context.ddgi_renderer->set_selection(get_producer_selection(source, Indirect_diffuse_source::ddgi));
    }
    if (context.radiance_cascades_renderer != nullptr) {
        context.radiance_cascades_renderer->set_selection(get_producer_selection(source, Indirect_diffuse_source::radiance_cascades));
    }
}

} // namespace editor
