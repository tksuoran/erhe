#include "renderers/indirect_diffuse.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "renderers/ddgi_renderer.hpp"
#include "renderers/radiance_cascades_renderer.hpp"

#include <algorithm>
#include <cmath>

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

void Temporal_history::reset(const int64_t item_count, const int64_t cursor)
{
    m_item_count      = item_count;
    m_origin          = (item_count > 0) ? (cursor % item_count) : 0;
    m_traced          = 0;
    m_active          = (item_count > 0);
    m_reset_requested = false;
    ++m_reset_count;
}

void Temporal_history::request_reset()
{
    m_reset_requested = true;
}

auto Temporal_history::begin_update(const int64_t item_count, const int64_t cursor) -> bool
{
    if (!m_reset_requested && (item_count == m_item_count)) {
        return false;
    }
    reset(item_count, cursor);
    return true;
}

auto Temporal_history::get_shader_parameters(const int64_t items_before_dispatch) const -> glm::uvec4
{
    return glm::uvec4{
        static_cast<uint32_t>(m_origin),
        static_cast<uint32_t>(m_traced + items_before_dispatch),
        static_cast<uint32_t>(m_item_count),
        m_active ? 1u : 0u
    };
}

void Temporal_history::end_update(const int64_t item_count_traced, const float hysteresis)
{
    if (!m_active) {
        return;
    }
    m_traced += item_count_traced;
    // Every item has been traced at least k_max + 1 times once
    // m_traced >= item_count * (k_max + 1); from then on
    // k / (k + 1) >= h for every item, and the plain hysteresis applies.
    const float   h     = std::clamp(hysteresis, 0.0f, 0.999f);
    const int64_t k_max = static_cast<int64_t>(std::ceil(h / (1.0f - h)));
    if (m_traced >= (m_item_count * (k_max + 1))) {
        m_active = false;
    }
}

auto Temporal_history::get_reset_count() const -> uint64_t
{
    return m_reset_count;
}

} // namespace editor
