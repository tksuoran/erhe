// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/scene_view.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"

#include "editor_message_bus.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/grid.hpp"
#include "tools/grid_tool.hpp"
#include "tools/tools.hpp"
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

static const std::string empty_string{};

void Hover_entry::reset()
{
    raytrace_node = nullptr;
    mesh.reset();
    grid = nullptr;
    geometry.reset();
}

[[nodiscard]] auto Hover_entry::get_name() const -> const std::string&
{
    if (mesh) {
        return mesh->get_name();
    }
    if (grid) {
        return grid->get_name();
    }
    return empty_string;
}

Scene_view::Scene_view(Editor_context& context, Viewport_config viewport_config)
    : m_context        {context}
    , m_viewport_config{viewport_config}
{
}

Scene_view::~Scene_view() noexcept = default;

void Scene_view::set_hover(
    const std::size_t  slot,
    const Hover_entry& entry
)
{
    const bool mesh_changed = m_hover_entries[slot].mesh != entry.mesh;
    const bool grid_changed = m_hover_entries[slot].grid != entry.grid;
    m_hover_entries[slot]      = entry;
    m_hover_entries[slot].slot = slot;

    if (mesh_changed || grid_changed) {
        m_context.editor_message_bus->send_message(
            Editor_message{
                .update_flags = Message_flag_bit::c_flag_bit_hover_mesh
            }
        );
    }
}

auto Scene_view::get_config() -> Viewport_config&
{
    return m_viewport_config;
}

auto Scene_view::get_light_projections() const -> const erhe::scene_renderer::Light_projections*
{
    auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr) {
        return nullptr;
    }
    erhe::scene_renderer::Light_projections& light_projections = shadow_render_node->get_light_projections();
    return &light_projections;
}

auto Scene_view::get_shadow_texture() const -> erhe::graphics::Texture*
{
    const auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr) {
        return nullptr;
    }
    return shadow_render_node->get_texture().get();
}

auto Scene_view::get_world_from_control() const -> std::optional<glm::mat4>
{
    return m_world_from_control;
}

auto Scene_view::get_control_from_world() const -> std::optional<glm::mat4>
{
    return m_control_from_world;
}

auto Scene_view::get_control_ray_origin_in_world() const -> std::optional<glm::vec3>
{
    if (!m_world_from_control.has_value()) {
        return {};
    }
    return glm::vec3{m_world_from_control.value() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

auto Scene_view::get_control_ray_direction_in_world() const -> std::optional<glm::vec3>
{
    if (!m_world_from_control.has_value()) {
        return {};
    }
    return glm::vec3{m_world_from_control.value() * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};
}

auto Scene_view::get_control_position_in_world_at_distance(
    const float distance
) const -> std::optional<glm::vec3>
{
    if (!m_world_from_control.has_value()) {
        return {};
    }

    const glm::vec3 origin    = get_control_ray_origin_in_world().value();
    const glm::vec3 direction = get_control_ray_direction_in_world().value();
    return origin + distance * direction;
}

auto Scene_view::get_hover(size_t slot) const -> const Hover_entry&
{
    return m_hover_entries.at(slot);
}

auto Scene_view::get_nearest_hover(uint32_t slot_mask) const -> const Hover_entry&
{
    std::size_t nearest_slot = 0;
    if (m_world_from_control.has_value()) {
        float nearest_distance = std::numeric_limits<float>::max();
        for (std::size_t slot = 0; slot < Hover_entry::slot_count; ++slot){
            const uint32_t slot_bit = (1 << slot);
            if ((slot_mask & slot_bit) == 0) {
                continue;
            }

            const Hover_entry& entry = m_hover_entries[slot];
            if (!entry.valid || !entry.position.has_value()) {
                continue;
            }
            const float distance = glm::distance(
                get_control_ray_origin_in_world().value(),
                entry.position.value()
            );
            if (distance < nearest_distance)
            {
                nearest_distance = distance;
                nearest_slot = slot;
            }
        }
    }
    return m_hover_entries.at(nearest_slot);
}

auto Scene_view::as_viewport_window() -> Viewport_window*
{
    return nullptr;
}

auto Scene_view::as_viewport_window() const -> const Viewport_window*
{
    return nullptr;
}

void Scene_view::set_world_from_control(
    glm::vec3 near_position_in_world,
    glm::vec3 far_position_in_world
)
{
    const auto camera = get_camera();
    if (!camera) {
        return;
    }
    const auto* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        return;
    }
    const glm::mat4 camera_world_from_node = camera_node->world_from_node();
    const glm::vec3 camera_up_in_world     = glm::vec3{camera_world_from_node * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::mat4 transform = erhe::toolkit::create_look_at(
        near_position_in_world,
        far_position_in_world,
        camera_up_in_world
    );
    set_world_from_control(transform);
}

void Scene_view::set_world_from_control(const glm::mat4& world_from_control)
{
    m_world_from_control = world_from_control;
    m_control_from_world = glm::inverse(world_from_control);
}

void Scene_view::reset_control_transform()
{
    for (std::size_t slot = 0; slot < Hover_entry::slot_count; ++slot) {
        set_hover(slot, Hover_entry{});
    }

    m_world_from_control.reset();
}

void Scene_view::reset_hover_slots()
{
    for (std::size_t slot = 0; slot < Hover_entry::slot_count; ++slot) {
        set_hover(slot, Hover_entry{});
    }
}

void Scene_view::update_grid_hover()
{
    const auto origin_opt    = get_control_ray_origin_in_world();
    const auto direction_opt = get_control_ray_direction_in_world();
    if (!origin_opt.has_value() || !direction_opt.has_value()) {
        return;
    }
    const glm::vec3 ray_origin    = origin_opt.value();
    const glm::vec3 ray_direction = direction_opt.value();

    const Grid_hover_position hover_position = m_context.grid_tool->update_hover(ray_origin, ray_direction);
    Hover_entry entry;
    entry.valid = (hover_position.grid != nullptr);
    if (entry.valid) {
        entry.position = hover_position.position;
        entry.normal   = glm::vec3{
            hover_position.grid->world_from_grid() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}
        };
        entry.grid     = hover_position.grid;
    }

    set_hover(Hover_entry::grid_slot, entry);
}

void Scene_view::update_hover_with_raytrace()
{
    const auto& scene_root = get_scene_root();
    if (!scene_root) {
        reset_hover_slots();
        return;
    }

    auto& scene = scene_root->raytrace_scene();
    scene.commit();

    ERHE_PROFILE_SCOPE("raytrace inner");

    const glm::vec3 ray_origin    = get_control_ray_origin_in_world   ().value();
    const glm::vec3 ray_direction = get_control_ray_direction_in_world().value();

    Scene_root* tool_scene_root = m_context.tools->get_tool_scene_root().get();
    for (std::size_t slot = 0; slot < Hover_entry::slot_count; ++slot) {
        const uint32_t slot_mask = Hover_entry::raytrace_slot_masks[slot];
        Hover_entry entry {
            .slot = slot,
            .mask = slot_mask
        };
        erhe::raytrace::Ray ray{
            .origin    = ray_origin,
            .t_near    = 0.0f,
            .direction = ray_direction,
            .time      = 0.0f,
            .t_far     = 9999.0f,
            .mask      = slot_mask,
            .id        = 0,
            .flags     = 0
        };
        erhe::raytrace::Hit hit;
        if (slot == Hover_entry::tool_slot) {
            if (tool_scene_root != nullptr) {
                tool_scene_root->raytrace_scene().intersect(ray, hit);
            }
        } else {
            scene.intersect(ray, hit);
        }
        entry.valid = (hit.instance != nullptr);
        if (entry.valid) {
            void* user_data     = hit.instance->get_user_data();
            entry.uv            = hit.uv;
            entry.raytrace_node = reinterpret_cast<Node_raytrace*>(user_data);
            entry.position      = ray.origin + ray.t_far * ray.direction;
            entry.local_index   = std::numeric_limits<std::size_t>::max();

            SPDLOG_LOGGER_TRACE(
                log_controller_ray,
                "{}: Hit position: {}",
                Hover_entry::slot_names[slot],
                entry.position.value()
            );

            if (entry.raytrace_node != nullptr) {
                auto* node = entry.raytrace_node->get_node();
                if (node != nullptr) {
                    SPDLOG_LOGGER_TRACE(
                        log_controller_ray,
                        "{}: Hit node: {}",
                        Hover_entry::slot_names[slot],
                        node->get_name()
                    );
                    const auto* rt_instance = entry.raytrace_node->raytrace_instance();
                    if (rt_instance != nullptr) {
                        SPDLOG_LOGGER_TRACE(
                            log_controller_ray,
                            "{}: RT instance: {}",
                            Hover_entry::slot_names[slot],
                            rt_instance->is_enabled()
                                ? "enabled"
                                : "disabled"
                        );
                    }
                }
                entry.mesh = get_mesh(node);
                if (entry.mesh) {
                    auto* primitive = entry.raytrace_node->raytrace_primitive();
                    if (primitive != nullptr) {
                        entry.geometry = entry.raytrace_node->source_geometry();
                        if (entry.geometry) {
                            SPDLOG_LOGGER_TRACE(
                                log_controller_ray,
                                "{}: Hit geometry: {}",
                                Hover_entry::slot_names[slot],
                                entry.geometry->name
                            );
                        }
                        if (hit.primitive_id < primitive->primitive_geometry.primitive_id_to_polygon_id.size()) {
                            const auto polygon_id = primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
                            SPDLOG_LOGGER_TRACE(
                                log_controller_ray,
                                "{}: Hit polygon: {}",
                                Hover_entry::slot_names[slot],
                                polygon_id
                            );
                            entry.local_index = polygon_id;

                            entry.normal = {};
                            if (entry.geometry != nullptr) {
                                if (polygon_id < entry.geometry->get_polygon_count()) {
                                    SPDLOG_LOGGER_TRACE(log_controller_ray, "hover polygon = {}", polygon_id);
                                    auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(
                                        erhe::geometry::c_polygon_normals
                                    );
                                    if (
                                        (polygon_normals != nullptr) &&
                                        polygon_normals->has(polygon_id)
                                    ) {
                                        const auto local_normal    = polygon_normals->get(polygon_id);
                                        const auto world_from_node = node->world_from_node();
                                        entry.normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                                        SPDLOG_LOGGER_TRACE(log_controller_ray, "hover normal = {}", entry.normal.value());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            SPDLOG_LOGGER_TRACE(
                log_controller_ray,
                "{}: no hit",
                Hover_entry::slot_names[slot]
            );
        }
        set_hover(slot, entry);
    }
}

auto Scene_view::get_closest_point_on_line(
    const glm::vec3 P0,
    const glm::vec3 P1
) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    const auto Q_origin_opt    = get_control_ray_origin_in_world();
    const auto Q_direction_opt = get_control_ray_direction_in_world();
    if (Q_origin_opt.has_value() && Q_direction_opt.has_value()) {
        const auto Q0 = Q_origin_opt.value();
        const auto Q1 = Q0 + Q_direction_opt.value();
        const auto closest_points_q = erhe::toolkit::closest_points<float>(P0, P1, Q0, Q1);
        if (closest_points_q.has_value()) {
            return closest_points_q.value().P;
        }
    }
    return {};
}

auto Scene_view::get_closest_point_on_plane(
    const glm::vec3 N,
    const glm::vec3 P
) -> std::optional<glm::vec3>
{
    using vec3 = glm::vec3;
    const auto Q_origin_opt    = get_control_ray_origin_in_world();
    const auto Q_direction_opt = get_control_ray_direction_in_world();
    if (
        !Q_origin_opt.has_value() ||
        !Q_direction_opt.has_value()
    ) {
        return {};
    }

    const vec3 Q0 = Q_origin_opt.value();
    const vec3 Q1 = Q0 + Q_direction_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<float>(N, P, Q0, v);
    if (!intersection.has_value()) {
        return {};
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    return drag_point_new_position_in_world;
}


} // namespace editor
