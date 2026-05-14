#include "erhe_scene_renderer/variant_handle.hpp"

namespace erhe::scene_renderer {

Variant_handle::Variant_handle(
    Shader_variant_cache& cache,
    Shader_variant_key    key
)
    : m_cache          {cache}
    , m_single_view_key{std::move(key)}
{
    m_cache.register_handle(this);
}

Variant_handle::Variant_handle(
    Shader_variant_cache& cache,
    Shader_variant_key    key,
    uint32_t              multiview_view_count
)
    : m_cache          {cache}
    , m_single_view_key{std::move(key)}
{
    // Build the multiview-sibling key once from the canonicalized
    // single-view key. The Shader_variant_key ctor re-runs canonicalize
    // and re-registers the shader name with Shader_name_registry; both
    // are idempotent and the cost is paid once at handle construction,
    // not on every resolve.
    m_multiview_key.emplace(
        m_single_view_key.name,
        m_single_view_key.defines,
        multiview_view_count,
        m_single_view_key.no_vertex_input,
        m_single_view_key.dump_interface
    );
    m_cache.register_handle(this);
}

Variant_handle::~Variant_handle() noexcept
{
    m_cache.unregister_handle(this);
}

auto Variant_handle::shader_stages() -> const erhe::graphics::Shader_stages*
{
    if (m_cached_shader_stages != nullptr) {
        return m_cached_shader_stages;
    }
    const erhe::graphics::Shader_stages* resolved = m_cache.get_or_compile(m_single_view_key);
    if (resolved != nullptr) {
        m_cached_shader_stages = resolved;
    }
    return resolved;
}

auto Variant_handle::multiview_shader_stages() -> const erhe::graphics::Shader_stages*
{
    if (!m_multiview_key.has_value()) {
        return nullptr;
    }
    if (m_cached_multiview_shader_stages != nullptr) {
        return m_cached_multiview_shader_stages;
    }
    const erhe::graphics::Shader_stages* resolved = m_cache.get_or_compile(*m_multiview_key);
    if (resolved != nullptr) {
        m_cached_multiview_shader_stages = resolved;
    }
    return resolved;
}

void Variant_handle::reset_memoization() noexcept
{
    m_cached_shader_stages           = nullptr;
    m_cached_multiview_shader_stages = nullptr;
}

} // namespace erhe::scene_renderer
