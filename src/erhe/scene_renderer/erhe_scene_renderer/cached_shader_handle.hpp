#pragma once

#include "erhe_graphics/lazy_shader_handle.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include <cstdint>

namespace erhe::scene_renderer {

// Concrete Lazy_shader_handle backed by a Shader_variant_cache. Stores
// the canonicalized single-view key plus the multiview view count for
// the optional multiview sibling. Two handles that name the same
// (name, defines, ...) tuple resolve to the same cache entry, so
// multiple call sites can hold separate handles to the same shader
// without paying for duplicate compiles.
//
// Construct via Programs (or any owner) once the cache is alive, then
// hand the handle pointer to Render_pipeline_create_info::lazy_shader_stages
// (or read shader_stages() / multiview_shader_stages() directly at
// the call site).
class Cached_shader_handle final : public erhe::graphics::Lazy_shader_handle
{
public:
    // Single-view-only handle: multiview_shader_stages() returns nullptr.
    Cached_shader_handle(
        Shader_variant_cache& cache,
        Shader_variant_key    key
    );

    // Single-view + multiview-sibling handle. The multiview key is built
    // by copying `key` and setting multiview_view_count to the supplied
    // value (must be >= 2). multiview_shader_stages() compiles and
    // returns that sibling on first call.
    Cached_shader_handle(
        Shader_variant_cache& cache,
        Shader_variant_key    key,
        uint32_t              multiview_view_count
    );

    ~Cached_shader_handle() noexcept override;

    Cached_shader_handle(const Cached_shader_handle&)            = delete;
    Cached_shader_handle(Cached_shader_handle&&)                 = delete;
    Cached_shader_handle& operator=(const Cached_shader_handle&) = delete;
    Cached_shader_handle& operator=(Cached_shader_handle&&)      = delete;

    [[nodiscard]] auto shader_stages           () -> const erhe::graphics::Shader_stages* override;
    [[nodiscard]] auto multiview_shader_stages () -> const erhe::graphics::Shader_stages* override;

private:
    Shader_variant_cache& m_cache;
    Shader_variant_key    m_single_view_key;

    // multiview sibling's view count. 0 means "this handle has no
    // multiview sibling" -- multiview_shader_stages() returns nullptr.
    uint32_t              m_multiview_view_count{0};
};

} // namespace erhe::scene_renderer
