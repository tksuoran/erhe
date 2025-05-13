// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/scene_view.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"

#include "editor_message_bus.hpp"
#include "scene/scene_root.hpp"
#include "tools/grid.hpp"
#include "tools/grid_tool.hpp"
#include "tools/tools.hpp"
#include "rendergraph/shadow_render_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_log/log_geogram.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/mesh/mesh_geometry.h>

#include <glm/gtx/matrix_operation.hpp>

namespace editor {

static const std::string empty_string{};

void Hover_entry::reset()
{
    *this = Hover_entry{};
}

auto Hover_entry::get_name() const -> const std::string&
{
    std::shared_ptr<erhe::scene::Mesh> scene_mesh = scene_mesh_weak.lock();
    if (scene_mesh) {
        return scene_mesh->get_name();
    }
    std::shared_ptr<const Grid> grid = grid_weak.lock();
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

void Scene_view::set_hover(const std::size_t slot, const Hover_entry& entry)
{
    std::shared_ptr<erhe::scene::Mesh> hower_scene_mesh = m_hover_entries[slot].scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover_entries[slot].grid_weak.lock();
    std::shared_ptr<erhe::scene::Mesh> entry_scene_mesh = entry.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        entry_grid       = entry.grid_weak.lock();
    const bool mesh_changed = (hower_scene_mesh != entry_scene_mesh);
    const bool grid_changed = (hover_grid       != entry_grid);
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

auto Scene_view::get_control_position_in_world_at_distance(const float distance) const -> std::optional<glm::vec3>
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

auto Scene_view::get_nearest_hover(uint32_t slot_mask) const -> const Hover_entry*
{
    std::optional<std::size_t> nearest_slot;
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
    if (!nearest_slot.has_value()) {
        return nullptr;
    }
    return &m_hover_entries.at(nearest_slot.value());
}

auto Scene_view::as_viewport_scene_view() -> Viewport_scene_view*
{
    return nullptr;
}

auto Scene_view::as_viewport_scene_view() const -> const Viewport_scene_view*
{
    return nullptr;
}

void Scene_view::set_world_from_control(glm::vec3 near_position_in_world, glm::vec3 far_position_in_world)
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
    const glm::mat4 transform = erhe::math::create_look_at(
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
        const glm::vec3 unit_normal_in_world = hover_position.grid->normal_in_world();
        entry.position  = hover_position.position;
        entry.normal    = unit_normal_in_world;
        entry.grid_weak = hover_position.grid;
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

    auto& rt_scene = scene_root->get_raytrace_scene();
    rt_scene.commit();

    ERHE_PROFILE_SCOPE("raytrace inner");

    const glm::vec3 ray_origin    = get_control_ray_origin_in_world   ().value();
    const glm::vec3 ray_direction = get_control_ray_direction_in_world().value();

    Scene_root* tool_scene_root = m_context.tools->get_tool_scene_root().get();

    // Optimization: Check if there are any hits. This helps to avoid doing
    // multiple masked raycasts later in case there are no hits at all.
    const bool any_hit = [&]() {
        erhe::raytrace::Ray ray{
            .origin    = ray_origin,
            .t_near    = 0.0f,
            .direction = ray_direction,
            .time      = 0.0f,
            .t_far     = 9999.0f,
            .mask      = std::numeric_limits<uint32_t>::max(),
            .id        = 0,
            .flags     = 0
        };
        erhe::raytrace::Hit hit;
        if (tool_scene_root != nullptr) {
            tool_scene_root->get_raytrace_scene().intersect(ray, hit);
            if (hit.instance != nullptr) {
                return true;
            }
        }
        rt_scene.intersect(ray, hit);
        if (hit.instance != nullptr) {
            return true;
        }
        return false;
    }();
    if (!any_hit) {
        reset_hover_slots();
        return;
    }

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
                tool_scene_root->get_raytrace_scene().intersect(ray, hit);
            }
        } else {
            rt_scene.intersect(ray, hit);
        }
        entry.valid = (hit.instance != nullptr);
        if (entry.valid && (ray.t_far > 0.0f)) {
            void* node_instance_user_data = hit.instance->get_user_data();
            auto* raytrace_primitive      = static_cast<erhe::scene::Raytrace_primitive*>(node_instance_user_data);
            ERHE_VERIFY(raytrace_primitive != nullptr);
            entry.uv                         = hit.uv;
            entry.position                   = ray.origin + ray.t_far * ray.direction;
            entry.normal                     = hit.normal;
            entry.triangle                   = std::numeric_limits<uint32_t>::max();
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit position: {}", Hover_entry::slot_names[slot], entry.position.value());
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit normal: {}", Hover_entry::slot_names[slot], hit.normal);
            std::shared_ptr scene_mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(raytrace_primitive->mesh->shared_from_this());
            ERHE_VERIFY(scene_mesh);
            entry.scene_mesh_weak            = scene_mesh;
            entry.scene_mesh_primitive_index = raytrace_primitive->primitive_index;
            auto* const node = scene_mesh->get_node();
            ERHE_VERIFY(node != nullptr);
            const std::vector<erhe::primitive::Primitive>& scene_mesh_primitives = scene_mesh->get_primitives();
            ERHE_VERIFY(raytrace_primitive->primitive_index < scene_mesh_primitives.size());
            const erhe::primitive::Primitive& scene_mesh_primitive = scene_mesh_primitives[raytrace_primitive->primitive_index];
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit node: {}", Hover_entry::slot_names[slot], node->get_name());
            ERHE_VERIFY(raytrace_primitive->rt_instance);
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: RT instance {}", Hover_entry::slot_names[slot], raytrace_primitive->rt_instance->is_enabled());
            const std::shared_ptr<erhe::primitive::Primitive_shape> shape = scene_mesh_primitive.get_shape_for_raytrace();
            ERHE_VERIFY(shape);
            entry.geometry = shape->get_geometry();
            if (entry.geometry) {
                GEO::Mesh& geo_mesh = entry.geometry->get_mesh();
                SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit geometry: {}", Hover_entry::slot_names[slot], entry.geometry->get_name());
                SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit triangle: {}", Hover_entry::slot_names[slot], hit.triangle_id);
                const GEO::index_t facet = shape->get_mesh_facet_from_triangle(hit.triangle_id);
                if (facet != GEO::NO_INDEX) {
                    ERHE_VERIFY(facet < geo_mesh.facets.nb());
                    SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit facet: {}", Hover_entry::slot_names[slot], facet);
                    entry.facet = facet;
                    const GEO::vec3f facet_normal           = mesh_facet_normalf(geo_mesh, facet);
                    const glm::vec3  local_normal           = to_glm_vec3(facet_normal);
                    const glm::mat4  world_from_node        = node->world_from_node();
                    const glm::mat4  normal_world_from_node = glm::transpose(glm::adjugate(world_from_node));
                    const glm::vec3  normal_in_world        = glm::vec3{normal_world_from_node * glm::vec4{local_normal, 0.0f}};
                    const glm::vec3  unit_normal_in_world   = glm::normalize(normal_in_world);
                    entry.normal = unit_normal_in_world;
                    SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit normal: {} ***", Hover_entry::slot_names[slot], unit_normal_in_world);
                }
            }
        } else {
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: no hit", Hover_entry::slot_names[slot]);
        }
        set_hover(slot, entry);
    }
}

auto Scene_view::get_closest_point_on_line(const glm::vec3 P0, const glm::vec3 P1) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    const auto Q_origin_opt    = get_control_ray_origin_in_world();
    const auto Q_direction_opt = get_control_ray_direction_in_world();
    if (Q_origin_opt.has_value() && Q_direction_opt.has_value()) {
        const auto Q0 = Q_origin_opt.value();
        const auto Q1 = Q0 + Q_direction_opt.value();
        const auto closest_points_q = erhe::math::closest_points<float>(P0, P1, Q0, Q1);
        if (closest_points_q.has_value()) {
            return closest_points_q.value().P;
        }
    }
    return {};
}

auto Scene_view::get_closest_point_on_plane(const glm::vec3 N, const glm::vec3 P) -> std::optional<glm::vec3>
{
    using vec3 = glm::vec3;
    const auto Q_origin_opt    = get_control_ray_origin_in_world();
    const auto Q_direction_opt = get_control_ray_direction_in_world();
    if (!Q_origin_opt.has_value() || !Q_direction_opt.has_value()) {
        return {};
    }

    const vec3 Q0 = Q_origin_opt.value();
    const vec3 Q1 = Q0 + Q_direction_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::math::intersect_plane<float>(N, P, Q0, v);
    if (!intersection.has_value()) {
        return {};
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    return drag_point_new_position_in_world;
}

} // namespace editor
