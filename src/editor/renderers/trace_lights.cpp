#include "renderers/trace_lights.hpp"

#include "app_context.hpp"
#include "renderers/render_settings.hpp"
#include "app_settings.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_utility/thread_roles.hpp"

#include <memory>
#include <vector>

namespace editor {

auto fit_trace_light_projections(
    App_context&                             context,
    erhe::graphics::Device&                  graphics_device,
    Scene_root&                              scene_root,
    erhe::scene_renderer::Light_projections& light_projections
) -> bool
{
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    // The scene's first camera as the copy the prepare phase of this frame
    // made (Scene_root::snapshot_render_settings).
    const uint64_t               frame_index           = graphics_device.get_frame_index();
    const Scene_render_settings& scene_render_settings = scene_root.get_render_settings(frame_index);
    if (!scene_render_settings.has_trace_camera) {
        return false;
    }
    // The light set as the same snapshot resolved it.
    static_cast<void>(context);
    const erhe::scene_renderer::Light_set& light_set = scene_root.get_light_set(frame_index);

    const erhe::graphics::Device_info& info = graphics_device.get_info();
    light_projections.apply(
        light_set,
        &scene_render_settings.trace_camera,
        erhe::math::Viewport{},
        erhe::math::Viewport{},
        {},     // no shadow map -> "no shadow map" sentinel in the light UBO
        graphics_device.get_reverse_depth(),
        info.coordinate_conventions.native_depth_range,
        info.coordinate_conventions
    );
    return true;
}

} // namespace editor
