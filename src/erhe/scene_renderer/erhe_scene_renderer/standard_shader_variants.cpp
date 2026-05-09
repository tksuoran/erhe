#include "erhe_scene_renderer/standard_shader_variants.hpp"

#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/standard_shader_variant.hpp"

#include "erhe_graphics/shader_stages.hpp"

namespace erhe::scene_renderer {

Standard_shader_variants::Standard_shader_variants(
    Shader_variant_cache&                cache,
    const erhe::graphics::Shader_stages& fallback_shader_stages
)
    : m_cache                {cache}
    , m_fallback_shader_stages{fallback_shader_stages}
{
}

Standard_shader_variants::~Standard_shader_variants() noexcept = default;

auto Standard_shader_variants::get_or_compile(const Standard_variant_key& key) -> const erhe::graphics::Shader_stages*
{
    // Adapt the typed key to a generic Shader_variant_key. The standard
    // shader is always single-view here -- multiview siblings are
    // requested through their own dedicated entry points (Forward_renderer
    // multiview path uses pre-built multiview_shader_stages on the
    // pipeline, which do not flow through this typed adapter).
    Shader_variant_key generic_key{
        std::string{"standard"},
        make_standard_variant_defines(key),
        /*multiview_view_count*/ 0u,
        /*no_vertex_input     */ false,
        /*dump_interface      */ false
    };

    const erhe::graphics::Shader_stages* stages = m_cache.get_or_compile(generic_key);
    if (stages != nullptr) {
        return stages;
    }
    return &m_fallback_shader_stages;
}

void Standard_shader_variants::clear()
{
    // Forwards to the shared Shader_variant_cache. This drops EVERY
    // cached shader -- not just standard variants -- because the cache
    // is shared across all shader consumers. Material edits that route
    // through here therefore also drop e.g. wide_lines / sky / debug
    // entries; they recompile on next use, which is acceptable for a
    // hammer-style invalidation. Per-name invalidation is a future
    // refinement.
    m_cache.clear();
}

auto Standard_shader_variants::size() const -> std::size_t
{
    return m_cache.size();
}

} // namespace erhe::scene_renderer
