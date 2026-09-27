#include "renderers/indirect_diffuse.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "renderers/ddgi_renderer.hpp"
#include "renderers/radiance_cascades_renderer.hpp"

namespace editor {

auto Probe_field::is_valid() const -> bool
{
    return parameters.is_valid() && irradiance && distance && probe_data;
}

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

auto get_indirect_diffuse_field(const App_context& context) -> Probe_field
{
    if (context.editor_settings == nullptr) {
        return Probe_field{};
    }
    switch (context.editor_settings->indirect_diffuse_source) {
        case Indirect_diffuse_source::ddgi: {
            return (context.ddgi_renderer != nullptr) ? context.ddgi_renderer->get_field() : Probe_field{};
        }
        case Indirect_diffuse_source::radiance_cascades: {
            return (context.radiance_cascades_renderer != nullptr) ? context.radiance_cascades_renderer->get_field() : Probe_field{};
        }
        default: {
            return Probe_field{};
        }
    }
}

} // namespace editor
