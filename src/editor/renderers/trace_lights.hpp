#pragma once

namespace erhe::graphics {
    class Device;
}
namespace erhe::scene_renderer {
    class Light_projections;
}

namespace editor {

class App_context;
class Scene_root;

// The light setup of a scene-global ray query consumer that shades hits
// with erhe_ray_hit.glsl (Ddgi_renderer, Radiance_cascades_renderer): the
// Light_buffer only writes the light slots present in the projections, so
// the consumer needs the scene's resolved light set and projection
// transforms even though it never samples a shadow map (it traces shadow
// rays).
//
// Resolves the scene root's light set with the limits the shadow render
// node uses (Light_set::resolve recomputes whenever the limits differ from
// the last call, so another set of limits would invalidate it every frame)
// and fits the projections around the first scene camera - the fitted
// transforms are unused by the ray query shaders, so any camera serves as
// the fit reference. Returns false, leaving the projections untouched, when
// the scene has no camera.
[[nodiscard]] auto fit_trace_light_projections(
    App_context&                             context,
    erhe::graphics::Device&                  graphics_device,
    Scene_root&                              scene_root,
    erhe::scene_renderer::Light_projections& light_projections
) -> bool;

} // namespace editor
