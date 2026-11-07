#include "tools/light_hover_provider.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/debug_visualizations.hpp"

#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"

#include <cmath>
#include <limits>

namespace editor {

Light_hover_provider::Light_hover_provider(App_context& context)
    : m_context{context}
{
}

auto Light_hover_provider::get_analytic_hover_name() const -> const std::string&
{
    static const std::string name{"Light"};
    return name;
}

auto Light_hover_provider::get_analytic_hover_slot() const -> uint32_t
{
    return Hover_entry::content_slot;
}

auto Light_hover_provider::get_analytic_hover_merge() const -> Analytic_hover_merge
{
    // The ring is drawn over the content (x-ray); lamp meshes often enclose the light.
    return Analytic_hover_merge::replace;
}

auto Light_hover_provider::pick_analytic_hover(
    Scene_view&     scene_view,
    const glm::vec3 ray_origin,
    const glm::vec3 ray_direction
) -> std::optional<Hover_entry>
{
    const std::shared_ptr<Scene_root>          scene_root = scene_view.get_scene_root();
    const std::shared_ptr<erhe::scene::Camera> camera     = scene_view.get_camera();
    if (!scene_root || !camera || (m_context.editor_settings == nullptr)) {
        return {};
    }
    const Debug_visualizations_settings& settings = scene_view.get_debug_visualizations_settings();
    if (settings.lights == Visualization_mode::off) {
        return {};
    }
    const Debug_visualizations_style& style           = m_context.editor_settings->debug_visualizations_style;
    const glm::vec3                   camera_position = glm::vec3{camera->position_in_world()};

    float                               best_t = std::numeric_limits<float>::max();
    std::shared_ptr<erhe::scene::Light> best_light;
    glm::vec3                           best_center{0.0f};
    for (const std::shared_ptr<erhe::scene::Light>& light : scene_root->layers().light()->lights) {
        if (!light || !Debug_visualizations::is_light_visualized(settings, *light)) {
            continue;
        }
        const std::optional<glm::vec3> visual_position = Debug_visualizations::get_light_visual_position(*light, *camera);
        if (!visual_position.has_value()) {
            continue;
        }
        const glm::vec3 center = visual_position.value();
        const std::optional<glm::vec2> radii = Debug_visualizations::get_light_ring_radii(style, scene_view, camera_position, center);
        if (!radii.has_value()) {
            continue;
        }
        const float     radius = radii->y;
        const glm::vec3 oc     = ray_origin - center;
        const float     b      = glm::dot(oc, ray_direction);
        const float     c      = glm::dot(oc, oc) - (radius * radius);
        const float     disc   = (b * b) - c;
        if (disc < 0.0f) {
            continue;
        }
        const float root = std::sqrt(disc);
        float t = -b - root;
        if (t < 0.0f) {
            t = -b + root;
        }
        if ((t < 0.0f) || (t >= best_t)) {
            continue;
        }
        best_t      = t;
        best_light  = light;
        best_center = center;
    }
    if (!best_light) {
        return {};
    }
    Hover_entry entry;
    entry.valid     = true;
    entry.position  = ray_origin + (best_t * ray_direction);
    entry.normal    = glm::normalize(entry.position.value() - best_center);
    entry.item_weak = best_light;
    return entry;
}

void Light_hover_provider::clear_analytic_hover(Scene_view&)
{
}

}
