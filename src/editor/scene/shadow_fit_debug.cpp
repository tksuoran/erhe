#include "scene/shadow_fit_debug.hpp"

#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/light_frustum_fit.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>

namespace editor {

using json = nlohmann::json;

namespace {

[[nodiscard]] auto to_json(const glm::vec3& v) -> json
{
    return json::array({v.x, v.y, v.z});
}

[[nodiscard]] auto to_json(const glm::vec4& v) -> json
{
    return json::array({v.x, v.y, v.z, v.w});
}

} // anonymous namespace

auto dump_shadow_fit_debug(const erhe::scene_renderer::Light_projections& light_projections) -> json
{
    json result = json::object();

    const erhe::scene::Camera* const view_camera = light_projections.parameters.view_camera;
    result["view_camera"]  = (view_camera != nullptr) ? view_camera->get_name() : std::string{};
    result["shadow_range"] = (view_camera != nullptr) ? view_camera->get_shadow_range() : 0.0f;

    const std::vector<erhe::scene::Light_projection_transforms>&   transforms     = light_projections.light_projection_transforms;
    const std::vector<erhe::scene::Shadow_frustum_fit_debug_data>& fit_debug_data = light_projections.fit_debug_data;

    json lights = json::array();
    const std::size_t count = std::min(transforms.size(), fit_debug_data.size());
    for (std::size_t i = 0; i < count; ++i) {
        const erhe::scene::Light* const                    light = transforms[i].light;
        const erhe::scene::Shadow_frustum_fit_debug_data&  fd    = fit_debug_data[i];

        json light_json = json::object();
        light_json["name"]                   = (light != nullptr) ? light->get_name() : std::string{};
        light_json["valid"]                  = fd.valid;
        light_json["view_frustum_corners_valid"] = fd.view_frustum_corners_valid;

        json view_frustum_corners = json::array();
        for (const glm::vec3& corner : fd.view_frustum_corners) {
            view_frustum_corners.push_back(to_json(corner));
        }
        light_json["view_frustum_corners"] = view_frustum_corners;

        json view_frustum_planes = json::array();
        for (const glm::vec4& plane : fd.view_frustum_planes) {
            view_frustum_planes.push_back(to_json(plane));
        }
        light_json["view_frustum_planes"] = view_frustum_planes;

        json planes = json::array();
        for (const glm::vec4& plane : fd.shadow_volume_planes) {
            planes.push_back(to_json(plane));
        }
        light_json["shadow_volume_plane_count"] = fd.shadow_volume_planes.size();
        light_json["shadow_volume_planes"]      = planes;

        json far_plane_hull = json::array();
        for (const glm::vec3& p : fd.receiver_far_plane_hull) {
            far_plane_hull.push_back(to_json(p));
        }
        light_json["receiver_far_plane_hull_count"] = fd.receiver_far_plane_hull.size();
        light_json["receiver_far_plane_hull"]       = far_plane_hull;

        // Receiver-cull diagnostics: the fit's receiver input set, the
        // intermediate point sets of the cull volume build, and the per-caster
        // cull verdicts (see Shadow_frustum_fit_debug_data).
        json receiver_boxes = json::array();
        for (const erhe::scene::Shadow_frustum_fit_debug_data::Receiver_box& box : fd.receiver_boxes) {
            json box_json = json::object();
            box_json["min"]        = to_json(box.aabb.min);
            box_json["max"]        = to_json(box.aabb.max);
            box_json["in_frustum"] = box.in_frustum;
            receiver_boxes.push_back(box_json);
        }
        light_json["receiver_box_count"] = fd.receiver_boxes.size();
        light_json["receiver_boxes"]     = receiver_boxes;

        const auto points_to_json = [](const std::vector<glm::vec3>& points) -> json {
            json result_points = json::array();
            for (const glm::vec3& p : points) {
                result_points.push_back(to_json(p));
            }
            return result_points;
        };
        light_json["receiver_hull_point_count"]         = fd.receiver_hull_points.size();
        light_json["receiver_hull_points"]              = points_to_json(fd.receiver_hull_points);
        light_json["receiver_clipped_point_count"]      = fd.receiver_clipped_points.size();
        light_json["receiver_clipped_points"]           = points_to_json(fd.receiver_clipped_points);
        light_json["receiver_clipped_hull_point_count"] = fd.receiver_clipped_hull_points.size();
        light_json["receiver_clipped_hull_points"]      = points_to_json(fd.receiver_clipped_hull_points);

        json receiver_filter_planes = json::array();
        for (const glm::vec4& plane : fd.receiver_filter_planes) {
            receiver_filter_planes.push_back(to_json(plane));
        }
        light_json["receiver_filter_plane_count"] = fd.receiver_filter_planes.size();
        light_json["receiver_filter_planes"]      = receiver_filter_planes;
        light_json["receiver_hull_path_used"]     = fd.receiver_hull_path_used;
        light_json["receiver_box_path_used"]      = fd.receiver_box_path_used;

        json caster_boxes = json::array();
        for (const erhe::scene::Shadow_frustum_fit_debug_data::Caster_box& box : fd.caster_boxes) {
            json box_json = json::object();
            box_json["min"]                       = to_json(box.aabb.min);
            box_json["max"]                       = to_json(box.aabb.max);
            box_json["culled_by_shadow_volume"]   = box.culled_by_shadow_volume;
            box_json["culled_by_receiver_volume"] = box.culled_by_receiver_volume;
            box_json["rejecting_plane"]           = box.rejecting_plane;
            caster_boxes.push_back(box_json);
        }
        light_json["caster_box_count"] = fd.caster_boxes.size();
        light_json["caster_boxes"]     = caster_boxes;

        lights.push_back(light_json);
    }
    result["lights"] = lights;

    return result;
}

} // namespace editor
