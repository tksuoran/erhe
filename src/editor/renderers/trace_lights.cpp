#include "renderers/trace_lights.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

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
    const std::vector<std::shared_ptr<erhe::scene::Camera>>& cameras = scene_root.get_scene().get_cameras();
    if (cameras.empty()) {
        return false;
    }
    const erhe::scene_renderer::Light_count_limits light_count_limits = (context.app_settings != nullptr)
        ? get_light_count_limits(context.app_settings->graphics.current_graphics_preset)
        : erhe::scene_renderer::Light_count_limits{};
    erhe::scene_renderer::Light_set& light_set = scene_root.get_light_set();
    light_set.resolve(scene_root.layers().light()->lights, light_count_limits);

    const erhe::graphics::Device_info& info = graphics_device.get_info();
    light_projections.apply(
        light_set,
        cameras.front().get(),
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
