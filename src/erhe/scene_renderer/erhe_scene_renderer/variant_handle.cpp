#if 0
#include "erhe_scene_renderer/variant_handle.hpp"

#include "erhe_graphics/shader_stages.hpp"

namespace erhe::scene_renderer {

Variant_handle::Variant_handle(
    Shader_variant_cache& cache,
    Shader_key            key
)
    : m_cache{cache}
    , m_key  {std::move(key)}
{
    m_cache.register_handle(this);
}

Variant_handle::~Variant_handle() noexcept
{
    m_cache.unregister_handle(this);
}

auto Variant_handle::shader_stages() -> const erhe::graphics::Shader_stages*
{
    // Shader_variant_cache::clear() invalidates entries in place rather
    // than freeing them, so the memoized pointer remains a valid memory
    // address but its is_valid() flips to false. Re-resolve through the
    // cache in that case -- compile_locked() then reloads the entry in
    // place and the returned address is identical to the memoized one.
    if ((m_cached_shader_stages != nullptr) && m_cached_shader_stages->is_valid()) {
        return m_cached_shader_stages;
    }
    const erhe::graphics::Shader_stages* resolved = m_cache.get_or_compile(m_key);
    if (resolved != nullptr) {
        m_cached_shader_stages = resolved;
    }
    return resolved;
}

} // namespace erhe::scene_renderer
#endif