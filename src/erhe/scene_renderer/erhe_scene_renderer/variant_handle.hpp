#pragma once

#include "erhe_graphics/shader_stages_handle.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include <cstdint>
#include <optional>

namespace erhe::scene_renderer {

// Concrete Shader_stages_handle backed by a Shader_variant_cache. Stores
// a canonicalized single-view Shader_variant_key plus, when constructed
// for multiview, a canonicalized sibling key. shader_stages() returns
// the cache entry for the single-view key; multiview_shader_stages()
// returns the entry for the multiview-sibling key (or nullptr when no
// multiview view count was supplied).
//
// Two handles that name the same (name, defines, ...) tuple resolve to
// the same cache entry, so multiple call sites can hold separate handles
// to the same shader without paying for duplicate compiles.
//
// Memoization. Once shader_stages() (resp. multiview_shader_stages())
// has produced a non-null Shader_stages*, the handle caches the pointer
// and returns it directly on every subsequent call -- the cache mutex is
// taken only on the first resolve and after Shader_variant_cache::clear()
// drops the entry. Clear() walks every registered handle and calls
// reset_memoization() so the next call re-resolves through the cache.
// Reads are not synchronized: every caller of shader_stages() and every
// caller of Shader_variant_cache::clear() runs on the editor's render
// thread (Material_change_operation, Shader_monitor::update_once_per_frame
// and the per-frame draw loop all execute there).
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
    // at construction by copying `key`'s name and defines and setting
    // multiview_view_count to the supplied value (must be >= 2). The
    // canonicalized key is stored so per-call resolves avoid a string +
    // vector copy and a second pass through the name registry.
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

    // Drop the memoized Shader_stages* pointers. Called by
    // Shader_variant_cache::clear() after it removes the underlying
    // entries so the cached pointers do not dangle past the next call.
    // The next shader_stages() / multiview_shader_stages() call will
    // re-resolve via get_or_compile and re-populate the memo.
    void reset_memoization() noexcept;

private:
    Shader_variant_cache& m_cache;
    Shader_variant_key    m_single_view_key;

    // Set iff the multiview ctor was used. nullopt means "this handle
    // has no multiview sibling" -- multiview_shader_stages() returns
    // nullptr.
    std::optional<Shader_variant_key> m_multiview_key;

    // Memoized Shader_stages*. nullptr means "not yet resolved", or
    // "the most recent resolve failed -- retry on the next call". Only
    // non-null successful resolves are cached so a transient compile
    // failure does not stick.
    const erhe::graphics::Shader_stages* m_cached_shader_stages          {nullptr};
    const erhe::graphics::Shader_stages* m_cached_multiview_shader_stages{nullptr};
};

} // namespace erhe::scene_renderer
