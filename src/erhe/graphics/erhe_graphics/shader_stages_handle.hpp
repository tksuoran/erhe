#pragma once

namespace erhe::graphics {

class Shader_stages;

// Stable indirection from a `Shader_stages_handle*` to a `Shader_stages*`.
// Consumers (notably `Render_pipeline_create_info::shader_stages_handle`)
// hold a pointer to a handle; the renderer calls `shader_stages()` (or
// `multiview_shader_stages()`) to obtain the underlying stages at the
// point of use.
//
// The interface lives in erhe_graphics so Render_pipeline_create_info
// can reference it without depending on the variant cache (which in turn
// depends on Program_interface and lives in erhe_scene_renderer).
//
// Returned pointers are stable for the lifetime of the handle's resolver
// and the caller does not own them.
//
// Resolution timing is unspecified: a backend may resolve in pipeline
// construction or at bind time. Callers must not assume the underlying
// compile happens at any particular point.
class Shader_stages_handle
{
public:
    virtual ~Shader_stages_handle() noexcept = default;

    // Single-view stages. Returns nullptr when the resolver cannot
    // produce stages (e.g. compile failure with no resolver-level
    // fallback). Fallback-on-compile-failure, if any, is the resolver's
    // responsibility, not this interface's.
    [[nodiscard]] virtual auto shader_stages() -> const Shader_stages* = 0;

    // Multiview-compiled sibling. Returns nullptr when this handle was
    // constructed without a multiview view count (i.e. the caller does
    // not need a multiview sibling) or when the resolver cannot produce
    // stages.
    [[nodiscard]] virtual auto multiview_shader_stages() -> const Shader_stages* = 0;
};

} // namespace erhe::graphics
