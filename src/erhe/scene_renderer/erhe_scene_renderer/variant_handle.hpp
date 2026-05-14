#pragma once

#include "erhe_graphics/shader_stages_handle.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include <cstdint>

namespace erhe::scene_renderer {

// Concrete Shader_stages_handle backed by a Shader_variant_cache. Stores
// a canonicalized single-view Shader_variant_key plus the multiview view
// count for the optional sibling. shader_stages() returns the cache
// entry for the single-view key; multiview_shader_stages() returns the
// entry for the multiview-sibling key (or nullptr when no multiview
// view count was supplied).
//
// Two handles that name the same (name, defines, ...) tuple resolve to
// the same cache entry, so multiple call sites can hold separate handles
// to the same shader without paying for duplicate compiles.
//
// Construct via Programs (or any owner) once the cache is alive, then
// hand the handle pointer to Render_pipeline_create_info::shader_stages_handle
// (or read shader_stages() / multiview_shader_stages() directly at
// the call site).
class Variant_handle final : public erhe::graphics::Shader_stages_handle
{
public:
    // Single-view-only handle: multiview_shader_stages() returns nullptr.
    Variant_handle(
        Shader_variant_cache& cache,
        Shader_variant_key    key
    );

    // Single-view + multiview-sibling handle. The multiview key is built
    // by copying `key` and setting multiview_view_count to the supplied
    // value (must be >= 2).
    Variant_handle(
        Shader_variant_cache& cache,
        Shader_variant_key    key,
        uint32_t              multiview_view_count
    );

    ~Variant_handle() noexcept override;

    Variant_handle(const Variant_handle&)            = delete;
    Variant_handle(Variant_handle&&)                 = delete;
    Variant_handle& operator=(const Variant_handle&) = delete;
    Variant_handle& operator=(Variant_handle&&)      = delete;

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
