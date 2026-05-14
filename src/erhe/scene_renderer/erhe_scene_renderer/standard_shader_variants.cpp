#include "erhe_scene_renderer/standard_shader_variants.hpp"

#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/standard_shader_variant.hpp"

#include "erhe_graphics/shader_stages.hpp"

namespace erhe::scene_renderer {

Standard_shader_variants::Standard_shader_variants(
    Shader_variant_cache& cache,
    Cached_shader_handle& fallback_handle
)
    : m_cache           {cache}
    , m_fallback_handle {fallback_handle}
{
}

Standard_shader_variants::~Standard_shader_variants() noexcept = default;

auto Standard_shader_variants::get_or_compile(
    const Standard_variant_key& key,
    const uint32_t              multiview_view_count
) -> const erhe::graphics::Shader_stages*
{
    // Adapt the typed key to a generic Shader_variant_key. The variant
    // cache keys on multiview_view_count, so single-view (0) and the
    // OpenXR multiview build (view count >= 2) get separate cache entries.
    Shader_variant_key generic_key{
        std::string{"standard"},
        make_standard_variant_defines(key),
        multiview_view_count,
        /*no_vertex_input     */ false,
        /*dump_interface      */ false
    };

    const erhe::graphics::Shader_stages* stages = m_cache.get_or_compile(generic_key);
    if (stages != nullptr) {
        return stages;
    }
    // Standard variant compile failed; fall back to the supplied
    // handle (typically programs.error). The handle is itself lazy --
    // it compiles its underlying shader on first access.
    return m_fallback_handle.shader_stages();
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
