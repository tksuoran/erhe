#include "erhe_scene_renderer/cached_shader_handle.hpp"

namespace erhe::scene_renderer {

Cached_shader_handle::Cached_shader_handle(
    Shader_variant_cache& cache,
    Shader_variant_key    key
)
    : m_cache               {cache}
    , m_single_view_key     {std::move(key)}
    , m_multiview_view_count{0}
{
}

Cached_shader_handle::Cached_shader_handle(
    Shader_variant_cache& cache,
    Shader_variant_key    key,
    uint32_t              multiview_view_count
)
    : m_cache               {cache}
    , m_single_view_key     {std::move(key)}
    , m_multiview_view_count{multiview_view_count}
{
}

Cached_shader_handle::~Cached_shader_handle() noexcept = default;

auto Cached_shader_handle::shader_stages() -> const erhe::graphics::Shader_stages*
{
    return m_cache.get_or_compile(m_single_view_key);
}

auto Cached_shader_handle::multiview_shader_stages() -> const erhe::graphics::Shader_stages*
{
    if (m_multiview_view_count == 0) {
        return nullptr;
    }

    // Build the multiview sibling key from the canonicalized single-view
    // key. Re-running the canonicalize ctor here is wasteful but cheap
    // (the defines list has been sorted/deduped already), and keeps
    // multiview view-count separate so the same handle works with any
    // configured view count without re-allocation.
    Shader_variant_key multiview_key{
        m_single_view_key.name,
        m_single_view_key.defines,
        m_multiview_view_count,
        m_single_view_key.no_vertex_input,
        m_single_view_key.dump_interface
    };
    return m_cache.get_or_compile(multiview_key);
}

} // namespace erhe::scene_renderer
