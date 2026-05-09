#pragma once

namespace erhe::graphics {

class Shader_stages;

// Polymorphic shader-stages handle that resolves on demand. Pipelines
// that want lazy shader compile hold a `Lazy_shader_handle*` instead of
// (or in addition to) a raw `Shader_stages*`; the renderer calls
// shader_stages() at draw time, which compiles the underlying shader
// on first use.
//
// The interface lives in erhe_graphics so Render_pipeline_create_info
// can reference it without depending on the variant cache (which
// in turn depends on Program_interface and lives in erhe_scene_renderer).
//
// Returned pointers are stable for the lifetime of the underlying
// resolver (typically a Shader_variant_cache); callers do not own
// them.
class Lazy_shader_handle
{
public:
    virtual ~Lazy_shader_handle() noexcept = default;

    // Single-view stages, compiled on first call. Returns the resolver's
    // fallback shader on compile failure, or nullptr if no fallback is
    // configured and the compile failed.
    [[nodiscard]] virtual auto shader_stages() -> const Shader_stages* = 0;

    // Multiview-compiled sibling, compiled on first call. Returns
    // nullptr when this handle was constructed without a multiview view
    // count (i.e. the caller does not need a multiview sibling).
    [[nodiscard]] virtual auto multiview_shader_stages() -> const Shader_stages* = 0;
};

} // namespace erhe::graphics
