// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/scene_view.hpp"

#include "editor_log.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "rendergraph/shadow_render_node.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor
{

[[nodiscard]] auto Scene_view::get_type() const -> int
{
    return Input_context_type_scene_view;
}

auto Scene_view::get_light_projections() const -> Light_projections*
{
    auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr)
    {
        return nullptr;
    }
    Light_projections& light_projections = shadow_render_node->get_light_projections();
    return &light_projections;
}

auto Scene_view::get_shadow_texture() const -> erhe::graphics::Texture*
{
    const auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr)
    {
        return nullptr;
    }
    return shadow_render_node->get_texture().get();
}

auto Scene_view::get_control_ray_origin_in_world() const -> std::optional<glm::dvec3>
{
    return m_control_ray_origin_in_world;
}

auto Scene_view::get_control_ray_direction_in_world() const -> std::optional<glm::dvec3>
{
    return m_control_ray_direction_in_world;
}

auto Scene_view::get_control_position_in_world_at_distance(
    const double distance
) const -> std::optional<glm::dvec3>
{
    if (
        !m_control_ray_origin_in_world.has_value() ||
        !m_control_ray_direction_in_world.has_value()
    )
    {
        return {};
    }

    const glm::dvec3 origin    = m_control_ray_origin_in_world.value();
    const glm::dvec3 direction = m_control_ray_direction_in_world.value();
    return origin + distance * direction;
}

auto Scene_view::get_position_in_viewport() const -> std::optional<glm::dvec2>
{
    return m_position_in_viewport;
}

auto Scene_view::get_hover(size_t slot) const -> const Hover_entry&
{
    return m_hover_entries.at(slot);
}

auto Scene_view::get_nearest_hover() const -> const Hover_entry&
{
    return m_hover_entries.at(m_nearest_slot);
}

void Scene_view::reset_control_ray()
{
    std::fill(
        m_hover_entries.begin(),
        m_hover_entries.end(),
        Hover_entry{}
    );

    m_control_ray_origin_in_world.reset();
    m_control_ray_direction_in_world.reset();
    m_position_in_viewport  .reset();
}

void Scene_view::raytrace_update(
    const glm::vec3 ray_origin,
    const glm::vec3 ray_direction,
    Scene_root*     tool_scene_root
)
{
    m_control_ray_origin_in_world    = ray_origin;
    m_control_ray_direction_in_world = ray_direction;

    const auto& scene_root = get_scene_root();
    if (!scene_root)
    {
        reset_control_ray();
        return;
    }

    auto& scene = scene_root->raytrace_scene();
    {
        scene.commit();
    }

    m_nearest_slot = 0;
    float nearest_t_far = 9999.0f;
    {
        ERHE_PROFILE_SCOPE("raytrace inner");

        for (size_t i = 0; i < Hover_entry::slot_count; ++i)
        {
            const uint32_t i_mask = Hover_entry::slot_masks[i];
            auto& entry = m_hover_entries[i];
            erhe::raytrace::Ray ray{
                .origin    = ray_origin,
                .t_near    = 0.0f,
                .direction = ray_direction,
                .time      = 0.0f,
                .t_far     = 9999.0f,
                .mask      = i_mask,
                .id        = 0,
                .flags     = 0
            };
            erhe::raytrace::Hit hit;
            if (i == Hover_entry::tool_slot)
            {
                if (tool_scene_root)
                {
                    tool_scene_root->raytrace_scene().intersect(ray, hit);
                }
            }
            else
            {
                scene.intersect(ray, hit);
            }
            entry.valid = hit.instance != nullptr;
            entry.mask  = i_mask;
            if (entry.valid)
            {
                void* user_data     = hit.instance->get_user_data();
                entry.raytrace_node = reinterpret_cast<Node_raytrace*>(user_data);
                entry.position      = ray.origin + ray.t_far * ray.direction;
                entry.geometry      = nullptr;
                entry.local_index   = std::numeric_limits<std::size_t>::max();

                SPDLOG_LOGGER_TRACE(
                    log_controller_ray,
                    "{}: Hit position: {}",
                    Hover_entry::slot_names[i],
                    entry.position.value()
                );

                if (entry.raytrace_node != nullptr)
                {
                    auto* node = entry.raytrace_node->get_node();
                    if (node != nullptr)
                    {
                        SPDLOG_LOGGER_TRACE(
                            log_controller_ray,
                            "{}: Hit node: {} {}",
                            Hover_entry::slot_names[i],
                            node->node_type(),
                            node->name()
                        );
                        const auto* rt_instance = entry.raytrace_node->raytrace_instance();
                        if (rt_instance != nullptr)
                        {
                            SPDLOG_LOGGER_TRACE(
                                log_controller_ray,
                                "{}: RT instance: {}",
                                Hover_entry::slot_names[i],
                                rt_instance->is_enabled()
                                    ? "enabled"
                                    : "disabled"
                            );
                        }
                    }
                    if (is_mesh(node))
                    {
                        entry.mesh = as_mesh(node->shared_from_this());
                        auto* primitive = entry.raytrace_node->raytrace_primitive();
                        entry.primitive = 0; // TODO
                        if (primitive != nullptr)
                        {
                            entry.geometry = entry.raytrace_node->source_geometry().get();
                            if (entry.geometry != nullptr)
                            {
                                SPDLOG_LOGGER_TRACE(
                                    log_controller_ray,
                                    "{}: Hit geometry: {}",
                                    Hover_entry::slot_names[i],
                                    entry.geometry->name
                                );
                            }
                            if (hit.primitive_id < primitive->primitive_geometry.primitive_id_to_polygon_id.size())
                            {
                                const auto polygon_id = primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
                                SPDLOG_LOGGER_TRACE(
                                    log_controller_ray,
                                    "{}: Hit polygon: {}",
                                    Hover_entry::slot_names[i],
                                    polygon_id
                                );
                                entry.local_index = polygon_id;

                                entry.normal = {};
                                if (entry.geometry != nullptr)
                                {
                                    if (polygon_id < entry.geometry->get_polygon_count())
                                    {
                                        SPDLOG_LOGGER_TRACE(log_controller_ray, "hover polygon = {}", polygon_id);
                                        auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
                                        if (
                                            (polygon_normals != nullptr) &&
                                            polygon_normals->has(polygon_id)
                                        )
                                        {
                                            const auto local_normal    = polygon_normals->get(polygon_id);
                                            const auto world_from_node = entry.mesh->world_from_node();
                                            entry.normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                                            SPDLOG_LOGGER_TRACE(log_controller_ray, "hover normal = {}", entry.normal.value());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (ray.t_far < nearest_t_far)
                {
                    m_nearest_slot = i;
                    nearest_t_far = ray.t_far;
                }
            }
            else
            {
                entry.raytrace_node = nullptr;
                entry.mesh.reset();
                entry.geometry      = nullptr;
                entry.position.reset();
                entry.normal.reset();
                entry.primitive     = std::numeric_limits<std::size_t>::max();
                entry.local_index   = std::numeric_limits<std::size_t>::max();

                SPDLOG_LOGGER_TRACE(
                    log_controller_ray,
                    "{}: no hit",
                    Hover_entry::slot_names[i]
                );
            }
        }
    }

    SPDLOG_LOGGER_TRACE(
        log_controller_ray,
        "Nearest slot: {}",
        Hover_entry::slot_names[m_nearest_slot]
    );
}

} // namespace editor
