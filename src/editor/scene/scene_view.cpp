// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/scene_view.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/editor_settings_config_serialization.hpp"
#include "editor_log.hpp"
#include "editor_settings_store.hpp"
#include "grid/grid.hpp"
#include "grid/grid_tool.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"
#include "windows/scene_view_config_window.hpp"
#include "windows/viewport_config_window.hpp"

#include "erhe_scene/scene.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_defer/defer.hpp"

#include <glm/gtx/matrix_operation.hpp>

#include <algorithm>

using erhe::geometry::mesh_facet_normalf;
using erhe::geometry::to_glm_vec3;

// #include "IconsMaterialDesignIcons.h"
#define ICON_MDI_AXIS_ARROW                               "\xf3\xb0\xb5\x89" // U+F0D49
#define ICON_MDI_CAMERA                                   "\xf3\xb0\x84\x80" // U+F0100
#define ICON_MDI_DOTS_TRIANGLE                            "\xf3\xb1\x97\xbe" // U+F15FE
#define ICON_MDI_EYE                                      "\xf3\xb0\x88\x88" // U+F0208
#define ICON_MDI_PALETTE                                  "\xf3\xb0\x8f\x98" // U+F03D8
#define ICON_MDI_EYE_SETTINGS_OUTLINE                     "\xf3\xb0\xa1\xae" // U+F086E

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

Scene_view::Scene_view(
    App_context&           context,
    Editor_settings_store* editor_settings_store,
    std::string_view       settings_key,
    Viewport_config        viewport_config
)
    : m_context              {context}
    , m_editor_settings_store{editor_settings_store}
    , m_settings_key         {settings_key}
    , m_viewport_config      {viewport_config}
{
    if ((m_editor_settings_store == nullptr) || m_settings_key.empty()) {
        return;
    }

    const Editor_settings_config& settings = m_editor_settings_store->get_settings();
    const auto i = std::find_if(
        settings.scene_views.begin(),
        settings.scene_views.end(),
        [this](const Scene_view_settings& entry) {
            return entry.name == m_settings_key;
        }
    );
    if (i != settings.scene_views.end()) {
        m_debug_visualizations.read_config(i->debug_visualizations);
        // Per-view Visual Style overrides the make_viewport_config() default.
        // (On the first run after upgrading a pre-v2 entry that has no saved
        // viewport_config, this adopts the codegen defaults once; the choice
        // then persists per view.)
        m_viewport_config = i->viewport_config;
        // Stage the persisted scene/camera selection; it is applied by name on
        // a later frame (see tick_scene_and_camera_restore) once the scene is
        // loaded, because resolving it needs App_scenes which a constructor
        // must not touch.
        m_pending_scene_and_camera         = i->scene_and_camera;
        m_scene_and_camera_restore_pending = true;
    } else {
        // No saved entry for this view yet: the global slot acts as the
        // defaults for new views. Keep the make_viewport_config() Visual Style
        // default and the constructor-provided scene/camera (nothing pending).
        m_debug_visualizations.read_config(settings.debug_visualizations);
    }

    m_collect_callback_id = m_editor_settings_store->register_collect_callback(
        [this](Editor_settings_config& settings_out) {
            auto j = std::find_if(
                settings_out.scene_views.begin(),
                settings_out.scene_views.end(),
                [this](const Scene_view_settings& entry) {
                    return entry.name == m_settings_key;
                }
            );
            if (j == settings_out.scene_views.end()) {
                Scene_view_settings new_entry{};
                new_entry.name = m_settings_key;
                settings_out.scene_views.push_back(std::move(new_entry));
                j = std::prev(settings_out.scene_views.end());
            }
            m_debug_visualizations.write_config(j->debug_visualizations);
            j->viewport_config = m_viewport_config;
            // Virtual: dispatches to Viewport_scene_view to add camera /
            // shader_debug / renderer_choice. Safe here because the callback
            // runs per frame, long after construction.
            write_scene_and_camera_settings(j->scene_and_camera);
        }
    );
}

Scene_view::~Scene_view() noexcept
{
    if (m_collect_callback_id.has_value()) {
        m_editor_settings_store->unregister_collect_callback(m_collect_callback_id.value());
    }
}

void Scene_view::set_scene_root(const std::shared_ptr<Scene_root>& scene_root)
{
    m_scene_root = scene_root;
}

auto Scene_view::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root.lock();
}

void Scene_view::render_debug_visualizations(const Render_context& context)
{
    m_debug_visualizations.render(context);
}

void Scene_view::set_shadow_fit_override_camera(const std::weak_ptr<erhe::scene::Camera>& camera)
{
    m_shadow_fit_override_camera = camera;
}

auto Scene_view::get_shadow_fit_override_camera() const -> std::weak_ptr<erhe::scene::Camera>
{
    return m_shadow_fit_override_camera;
}

auto Scene_view::get_reverse_depth() const -> bool
{
    // Single source of truth: the device derives the effective reverse-Z
    // choice from Graphics_config::force_disable_reverse_depth and API support.
    return m_context.graphics_device->get_reverse_depth();
}

auto Scene_view::get_depth_range() const -> erhe::math::Depth_range
{
    return m_context.graphics_device->get_info().coordinate_conventions.native_depth_range;
}

auto Scene_view::get_framebuffer_origin() const -> erhe::math::Framebuffer_origin
{
    return m_context.graphics_device->get_info().coordinate_conventions.framebuffer_origin;
}

auto Scene_view::get_conventions() const -> erhe::math::Coordinate_conventions
{
    return m_context.graphics_device->get_info().coordinate_conventions;
}

void Scene_view::set_hover(const std::size_t slot, const Hover_entry& entry)
{
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover_entries[slot].scene_mesh_weak.lock();
    std::shared_ptr<Grid>              hover_grid       = m_hover_entries[slot].grid_weak.lock();
    std::shared_ptr<erhe::scene::Mesh> entry_scene_mesh = entry.scene_mesh_weak.lock();
    std::shared_ptr<Grid>              entry_grid       = entry.grid_weak.lock();
    const bool mesh_changed = (hover_scene_mesh != entry_scene_mesh);
    const bool grid_changed = (hover_grid       != entry_grid);
    m_hover_entries[slot]      = entry;
    m_hover_entries[slot].slot = slot;
    if (mesh_changed || grid_changed) {
        m_hover_update_pending = true;
    }
}

void Scene_view::merge_hover(const std::size_t slot, const Hover_entry& candidate)
{
    // Invalid candidates never override what is already in the slot --
    // an empty Hover_entry from the path that did not hit must not erase
    // a valid hit from the path that did.
    if (!candidate.valid || !candidate.position.has_value()) {
        return;
    }

    const Hover_entry& current = m_hover_entries[slot];
    if (!current.valid || !current.position.has_value()) {
        // Slot is empty (or was just cleared): take the candidate.
        set_hover(slot, candidate);
        return;
    }

    // Both have a hit -- keep whichever is closer along the picking ray.
    // ray.direction is unit-length (set_world_from_control normalizes
    // far - near), so the dot product yields a signed metric distance.
    const std::optional<glm::vec3> ray_origin_opt    = get_control_ray_origin_in_world();
    const std::optional<glm::vec3> ray_direction_opt = get_control_ray_direction_in_world();
    if (!ray_origin_opt.has_value() || !ray_direction_opt.has_value()) {
        return;
    }
    const glm::vec3 ray_origin    = ray_origin_opt.value();
    const glm::vec3 ray_direction = ray_direction_opt.value();
    const float current_t   = glm::dot(current.position.value()   - ray_origin, ray_direction);
    const float candidate_t = glm::dot(candidate.position.value() - ray_origin, ray_direction);
    if (candidate_t < current_t) {
        set_hover(slot, candidate);
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
            if (distance < nearest_distance) {
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
    // Skips skinned meshes (mask bit Raytrace_node_mask::skinned) because
    // their BVH is rest-pose only -- they are picked by the ID renderer.
    const bool any_hit = [&]() {
        erhe::raytrace::Ray ray{
            .origin    = ray_origin,
            .t_near    = 0.0f,
            .direction = ray_direction,
            .time      = 0.0f,
            .t_far     = 9999.0f,
            .mask      = Raytrace_node_mask::pickable_static,
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
            const std::vector<erhe::scene::Mesh_primitive>& scene_mesh_primitives = scene_mesh->get_primitives();
            ERHE_VERIFY(raytrace_primitive->primitive_index < scene_mesh_primitives.size());
            const erhe::scene::Mesh_primitive& scene_mesh_primitive = scene_mesh_primitives[raytrace_primitive->primitive_index];
            const erhe::primitive::Primitive&  primitive            = *scene_mesh_primitive.primitive.get();
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit node: {}", Hover_entry::slot_names[slot], node->get_name());
            ERHE_VERIFY(raytrace_primitive->rt_instance);
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: RT instance {}", Hover_entry::slot_names[slot], raytrace_primitive->rt_instance->is_enabled());
            const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive.get_shape_for_raytrace();
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
                    const bool       negative_determinant   = (node->get_flag_bits() & erhe::Item_flags::negative_determinant) == erhe::Item_flags::negative_determinant;
                    const GEO::vec3f facet_normal           = mesh_facet_normalf(geo_mesh, facet);
                    const glm::vec3  local_normal           = to_glm_vec3(facet_normal);
                    const glm::mat4  world_from_node        = node->world_from_node();
                    const glm::mat4  normal_world_from_node = glm::transpose(glm::adjugate(world_from_node));
                    const glm::vec3  normal_in_world        = glm::vec3{normal_world_from_node * glm::vec4{local_normal, 0.0f}};
                    const glm::vec3  unit_normal_in_world   = glm::normalize(normal_in_world);
                    entry.normal = negative_determinant ? -unit_normal_in_world : unit_normal_in_world;
                    SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: Hit normal: {} ***", Hover_entry::slot_names[slot], unit_normal_in_world);
                }
            }
        } else {
            SPDLOG_LOGGER_TRACE(log_controller_ray, "{}: no hit", Hover_entry::slot_names[slot]);
        }
        // Raytrace is the primary path: it fully owns the slot it writes
        // to, overwriting whatever the previous frame left. The ID
        // renderer runs after this and merges its (possibly closer)
        // candidate via merge_hover. Headset_view also calls
        // update_hover_with_raytrace as its only picker and relies on
        // the overwrite semantic.
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

void Scene_view::update_transforms()
{
    // Apply any persisted scene/camera selection now that scenes are loaded.
    // Runs before the early-out below so a view that needs its scene restored
    // is not skipped while it still has no scene_root.
    tick_scene_and_camera_restore();

    std::shared_ptr<Scene_root> scene_root = get_scene_root();
    if (!scene_root) {
        return;
    }

    erhe::scene::Scene& scene = scene_root->get_scene();
    scene.update_node_transforms();
}

void Scene_view::tick_scene_and_camera_restore()
{
    if (!m_scene_and_camera_restore_pending) {
        return;
    }
    if (resolve_pending_scene_and_camera()) {
        m_scene_and_camera_restore_pending = false;
    }
}

auto Scene_view::find_camera_in_scene(
    const Scene_root&  scene_root,
    const std::string& name
) -> std::shared_ptr<erhe::scene::Camera>
{
    if (name.empty()) {
        return {};
    }
    const std::vector<std::shared_ptr<erhe::scene::Camera>>& cameras = scene_root.get_scene().get_cameras();
    for (const std::shared_ptr<erhe::scene::Camera>& camera : cameras) {
        if (camera->get_name() == name) {
            return camera;
        }
    }
    return {};
}

void Scene_view::write_scene_and_camera_settings(Scene_and_camera_settings& out) const
{
    if (m_scene_and_camera_restore_pending) {
        // Not yet restored: preserve the persisted selection verbatim so the
        // baseline collect cannot clobber it before resolution completes.
        out.scene_name                      = m_pending_scene_and_camera.scene_name;
        out.shadow_fit_override_camera_name = m_pending_scene_and_camera.shadow_fit_override_camera_name;
        return;
    }
    std::shared_ptr<Scene_root> scene_root = get_scene_root();
    out.scene_name = scene_root ? scene_root->get_name() : std::string{};
    std::shared_ptr<erhe::scene::Camera> shadow_camera = m_shadow_fit_override_camera.lock();
    out.shadow_fit_override_camera_name = shadow_camera ? shadow_camera->get_name() : std::string{};
}

auto Scene_view::resolve_pending_scene_and_camera() -> bool
{
    const std::string& scene_name = m_pending_scene_and_camera.scene_name;
    std::shared_ptr<Scene_root> scene_root = get_scene_root();
    if (!scene_name.empty()) {
        std::shared_ptr<Scene_root> found;
        for (const std::shared_ptr<Scene_root>& root : m_context.app_scenes->get_scene_roots()) {
            if (root->get_name() == scene_name) {
                found = root;
                break;
            }
        }
        if (!found) {
            return false; // named scene not loaded yet; retry on a later frame
        }
        set_scene_root(found);
        scene_root = found;
    }
    const std::string& shadow_name = m_pending_scene_and_camera.shadow_fit_override_camera_name;
    if (!shadow_name.empty() && scene_root) {
        set_shadow_fit_override_camera(find_camera_in_scene(*scene_root, shadow_name));
    }
    return true;
}

auto Scene_view::icon_button(ImFont* icon_font, float font_size, const char* icon, const char* fallback_text, const char* tooltip, bool& toggle) -> bool
{
    bool pressed;
    erhe::imgui::Item_mode mode = toggle ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
    erhe::imgui::begin_button_style(mode);
    if (icon_font != nullptr) {
        ImGui::PushFont(icon_font, font_size);
        pressed = ImGui::Button(icon);
        ImGui::PopFont();
    } else {
        pressed = ImGui::Button(fallback_text);
    }
    erhe::imgui::end_button_style(mode);
    if ((tooltip != nullptr) && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    if (pressed) {
        toggle = !toggle;
    }
    return pressed;
}

void Scene_view::popup_button(
    ImFont*                      icon_font,
    float                        font_size,
    const char*                  icon,
    const char*                  title,
    ImGuiID                      popup_id,
    bool&                        is_open,
    const std::function<void()>& content_fn
)
{
    ImGui::PushFont(icon_font, font_size);
    const bool pressed = ImGui::Button(icon);
    ImGui::PopFont();
    ImGui::SetItemTooltip("%s", title);
    if (pressed) {
        is_open = true;
        ImGui::OpenPopup(popup_id, ImGuiPopupFlags_None);
    }
    if (erhe::imgui::begin_popup_with_title_and_open(popup_id, title, &is_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        content_fn();
        ImGui::EndPopup();
    }
}

void Scene_view::viewport_toolbar()
{
    ImGui::PushID("Viewport_scene_view::viewport_toolbar()");
    ERHE_DEFER( ImGui::PopID(); );

    ImFont* icon_font = m_context.imgui_renderer->material_design_font();
    const float font_size =
        m_context.imgui_renderer->get_imgui_settings().scale_factor *
        m_context.imgui_renderer->get_imgui_settings().material_design_font_size;

#if 0
    icon_button(
        icon_font,
        font_size,
        ICON_MDI_AXIS_ARROW "##navigation_gizmo",
        "N##navigation_gizmo",
        "Show/Hide Navigation Gizmo",
        m_show_navigation_gizmo
    );
#endif

    popup_button(
        icon_font,
        font_size,
        ICON_MDI_EYE "##ViewportVisualStyle",                      // icon
        "Visual Style",                                            // title
        ImGui::GetID("ViewportVisualStylePopup"),                  // popup_id
        m_show_visual_style_popup,                                 // is_open
        [this]() { Viewport_config_window::imgui(get_config()); }  // content_fn
    );
    popup_button(
        icon_font,
        font_size,
        ICON_MDI_DOTS_TRIANGLE "##ViewportSceneAndCamera",              // icon
        "Scene and Camera",                                             // title
        ImGui::GetID("ViewportSceneAndCameraPopup"),                    // popup_id
        m_show_scene_and_camera_popup,                                  // is_open
        [this]() { Scene_view_config_window::imgui(m_context, *this); } // content_fn
    );
    popup_button(
        icon_font,
        font_size,
        ICON_MDI_EYE_SETTINGS_OUTLINE "##ViewportDebugVisualizations", // icon
        "Debug Visualization",                                         // title
        ImGui::GetID("ViewportDebugVisualizations"),                   // popup_id
        m_show_debug_visualizations_popup,                             // is_open
        [this]() { m_debug_visualizations.imgui(*this, m_context); }   // content_fn
    );

    m_context.selection_tool->viewport_toolbar();
    m_context.transform_tool->viewport_toolbar();
    //// m_context.grid_tool->viewport_toolbar(hovered);
    //// TODO m_physics_window.viewport_toolbar(hovered);
}

}
