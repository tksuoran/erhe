#include "tools/debug_visualizations.hpp"

#include "config/generated/debug_visualizations_settings.hpp"
#include "config/generated/editor_settings_config.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_settings.hpp"
#include "editor_log.hpp"
#include "scene/shadow_fit_debug.hpp"
#include "renderers/render_context.hpp"
#include "renderers/programs.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/texel_renderer.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
#   include <Jolt/Jolt.h>
#   include "erhe_physics/iworld.hpp"
#   if defined(JPH_DEBUG_RENDERER)
#       include "erhe_renderer/jolt_debug_renderer.hpp"
#   endif
#endif

#include <geogram/mesh/mesh_geometry.h>

using erhe::geometry::Mesh_attributes;
using erhe::geometry::get_pointf;
using erhe::geometry::to_glm_vec3;
using erhe::geometry::mesh_facet_normalf;

namespace editor {

using glm::mat4;
using glm::vec3;
using glm::vec4;
using Trs_transform = erhe::scene::Trs_transform;

namespace {

[[nodiscard]] auto sign(const float x) -> float { return x < 0.0f ? -1.0f : 1.0f; }

constexpr vec3 clip_min_corner{-1.0f, -1.0f, 0.0f};
constexpr vec3 clip_max_corner{ 1.0f,  1.0f, 1.0f};
constexpr vec3 O              { 0.0f };
constexpr vec3 axis_x         { 1.0f,  0.0f, 0.0f};
constexpr vec3 axis_y         { 0.0f,  1.0f, 0.0f};
constexpr vec3 axis_z         { 0.0f,  0.0f, 1.0f};

[[nodiscard]] auto should_visualize(const Visualization_mode mode, const bool is_selected, const bool is_hovered)
{
    if (mode == Visualization_mode::all) {
        return true;
    }
    if (is_selected && (mode == Visualization_mode::selected)) {
        return true;
    }
    if (is_hovered && (mode == Visualization_mode::hovered)) {
        return true;
    }
    return false;
}

[[nodiscard]] auto should_visualize(const Visualization_mode mode, const std::shared_ptr<erhe::Item_base>& item)
{
    return should_visualize(mode, item->is_selected(), false);
}

[[nodiscard]] auto should_visualize(const Visualization_mode mode, const erhe::Item_base* const item)
{
    return should_visualize(mode, item->is_selected(), false);
}

}

void Debug_visualizations::read_config(const Debug_visualizations_settings& settings)
{
    m_settings = settings;
}

void Debug_visualizations::write_config(Debug_visualizations_settings& settings) const
{
    settings = m_settings;
}
auto Debug_visualizations::get_selected_camera(const Render_context& render_context) -> std::shared_ptr<erhe::scene::Camera>
{
    const auto* scene     = render_context.get_scene();
    const auto& selection = render_context.app_context.selection->get_selected_items();

    for (const auto& item : selection) {
        const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (node) {
            if (node->get_scene() != scene) {
                continue;
            }
            for (const auto& attachment : node->get_attachments()) {
                const auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(attachment);
                if (camera) {
                    return camera;
                }
            }
        }
    }

    return {};
}

void Debug_visualizations::mesh_visualization(const Render_context& render_context, erhe::scene::Mesh* mesh)
{
    if (mesh == nullptr) {
        return;
    }
    erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    const auto* node = mesh->get_node();
    if (node == nullptr) {
        return;
    }

    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    std::shared_ptr<erhe::scene::Camera> selected_camera  = get_selected_camera(render_context);
    std::shared_ptr<erhe::scene::Camera> context_camera   = render_context.scene_view.get_camera();
    std::shared_ptr<erhe::scene::Camera> used_camera      = selected_camera ? selected_camera : context_camera;
    erhe::scene::Node*                   used_camera_node = used_camera ? used_camera->get_node() : nullptr;

    Trs_transform camera_world_from_node_transform = (used_camera_node != nullptr)
        ? used_camera_node->world_from_node_transform()
        : Trs_transform{};

    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        const erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
        if (!primitive.render_shape) {
            continue;
        }
        const erhe::primitive::Buffer_mesh& buffer_mesh = primitive.render_shape->get_renderable_mesh();

        const float box_volume    = buffer_mesh.bounding_box.volume();
        const float sphere_volume = buffer_mesh.bounding_sphere.volume();
        //const bool  smallest_visualization = !m_viewport_config->selection_bounding_box && !m_viewport_config->selection_bounding_sphere;
        const bool  box_smaller   = box_volume < sphere_volume;

        if (box_smaller) {
            m_selection_bounding_volume.add_box(
                node->world_from_node(),
                buffer_mesh.bounding_box.min,
                buffer_mesh.bounding_box.max
            );
        } else {
            m_selection_bounding_volume.add_sphere(
                node->world_from_node(),
                buffer_mesh.bounding_sphere.center,
                buffer_mesh.bounding_sphere.radius
            );
        }

        if (m_settings.selection_parts) {
            if (m_settings.selection_box || (box_smaller && !m_settings.selection_sphere)) {
                line_renderer.set_thickness(m_settings.selection_major_width);
                line_renderer.add_cube(
                    node->world_from_node(),
                    m_settings.selection_major_color,
                    buffer_mesh.bounding_box.min - glm::vec3{m_settings.gap, m_settings.gap, m_settings.gap},
                    buffer_mesh.bounding_box.max + glm::vec3{m_settings.gap, m_settings.gap, m_settings.gap}
                );
            }
            if (m_settings.selection_sphere || (!box_smaller && !m_settings.selection_box)) {
                if (used_camera) {
                    line_renderer.add_sphere(
                        node->world_from_node_transform(),
                        m_settings.selection_major_color,
                        m_settings.selection_minor_color,
                        m_settings.selection_major_width,
                        m_settings.selection_minor_width,
                        buffer_mesh.bounding_sphere.center,
                        buffer_mesh.bounding_sphere.radius + m_settings.gap,
                        &camera_world_from_node_transform,
                        m_settings.sphere_step_count
                    );
                }
            }
        }
    }
}

void Debug_visualizations::skin_visualization(const Render_context& render_context, erhe::scene::Skin& skin)
{
    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    std::shared_ptr<erhe::scene::Camera> selected_camera  = get_selected_camera(render_context);
    std::shared_ptr<erhe::scene::Camera> context_camera   = render_context.scene_view.get_camera();
    std::shared_ptr<erhe::scene::Camera> used_camera      = selected_camera ? selected_camera : context_camera;
    erhe::scene::Node*                   used_camera_node = used_camera ? used_camera->get_node() : nullptr;

    Trs_transform camera_world_from_node_transform = (used_camera_node != nullptr)
        ? used_camera_node->world_from_node_transform()
        : Trs_transform{};

    //constexpr vec4 red  { 1.0f, 0.0f, 0.0f, 1.0f};
    //constexpr vec4 green{ 0.0f, 1.0f, 0.0f, 1.0f};
    //constexpr vec4 blue { 0.0f, 0.0f, 1.0f, 1.0f};
    //constexpr vec4 cyan { 0.0f, 1.0f, 1.0f, 1.0f};
    line_renderer.set_thickness(2.0f);

    for (std::size_t i = 0, end_i = skin.skin_data.joints.size(); i < end_i; ++i) {
        const auto& joint = skin.skin_data.joints[i];
        if (!joint) {
            continue;
        }
        const mat4 world_from_joint = joint->world_from_node();

        //// line_renderer.set_line_color(joint->get_wireframe_color());
        line_renderer.set_line_color(glm::vec4{0.0f, 1.0f, 1.0f, 1.0});
        vec3 a = joint->position_in_world();
        vec3 b = a + vec3{0.2f, 0.0f, 0.0f};

        // Search for child to connect bone tip:
        bool child_found = false;
        for (std::size_t j = 0, end_j = skin.skin_data.joints.size(); j < end_j; ++j) {
            if (j == i) {
                continue;
            }
            const auto& other_joint = skin.skin_data.joints[j];
            if (other_joint->get_parent_node() == joint) {
                b = other_joint->position_in_world();
                child_found = true;
                break;
            }
        }
        if (!child_found) {
            // No child, try to guess bone tip compared to parent (if it has parent):
            const auto& parent = joint->get_parent_node();
            if (parent) {
                const vec3 parent_position = parent->position_in_world();
                const float distance = glm::distance(parent_position, a);
                vec3 joint_local_axis_y = joint->transform_direction_from_local_to_world(axis_y);
                b = a + distance * joint_local_axis_y;
            }
        }

        // For linear blend skinning, matrices to be used on the shader would be:
        //const mat4  joint_from_bind  = skin->skin_data.inverse_bind_matrices[i];
        //const mat4  world_from_bind  = world_from_joint * joint_from_bind;
        float side_length = 0.1f * glm::distance(a, b);
        vec3 mid_point = glm::mix(a, b, 0.1f);
        vec3 joint_local_axis_x = joint->transform_direction_from_local_to_world(axis_x);
        vec3 joint_local_axis_z = joint->transform_direction_from_local_to_world(axis_z);
        vec3 m1 = mid_point + side_length * joint_local_axis_x;
        vec3 m2 = mid_point + side_length * joint_local_axis_z;
        vec3 m3 = mid_point - side_length * joint_local_axis_x;
        vec3 m4 = mid_point - side_length * joint_local_axis_z;
        //line_renderer.add_lines( world_from_joint, red,   { { side_length * axis_x }});
        //line_renderer.add_lines( world_from_joint, green, { { side_length * axis_y }});
        //line_renderer.add_lines( world_from_joint, blue,  { { side_length * axis_z }});
        //line_renderer.add_lines( cyan,  { { a, b }});
        line_renderer.add_lines( {{ a,  m1 }} );
        line_renderer.add_lines( {{ a,  m2 }} );
        line_renderer.add_lines( {{ a,  m3 }} );
        line_renderer.add_lines( {{ a,  m4 }} );
        line_renderer.add_lines( {{ b,  m1 }} );
        line_renderer.add_lines( {{ b,  m2 }} );
        line_renderer.add_lines( {{ b,  m3 }} );
        line_renderer.add_lines( {{ b,  m4 }} );
        line_renderer.add_lines( {{ m1, m2 }} );
        line_renderer.add_lines( {{ m2, m3 }} );
        line_renderer.add_lines( {{ m3, m4 }} );
        line_renderer.add_lines( {{ m4, m1 }} );
    }
}

void Debug_visualizations::light_visualization(
    const Render_context&                render_context,
    std::shared_ptr<erhe::scene::Camera> selected_camera,
    const erhe::scene::Light*            light
)
{
    ERHE_PROFILE_FUNCTION();

    using namespace erhe::utility;
    if (!test_bit_set(light->get_flag_bits(), erhe::Item_flags::show_debug_visualizations)) {
        return;
    }

    const Light_visualization_context light_context{
        .render_context   = render_context,
        .selected_camera  = selected_camera,
        .light            = light,
        .light_color      = glm::vec4{light->color, 1.0f},
        .half_light_color = glm::vec4{0.5f * light->color, 0.5f}
    };

    switch (light->type) {
        case erhe::scene::Light_type::directional: directional_light_visualization(light_context); break;
        case erhe::scene::Light_type::point:       point_light_visualization      (light_context); break;
        case erhe::scene::Light_type::spot:        spot_light_visualization       (light_context); break;
        default: break;
    }
}

void Debug_visualizations::directional_light_visualization(const Light_visualization_context& context)
{
    const auto shadow_render_node = context.render_context.app_context.app_rendering->get_shadow_node_for_view(context.render_context.scene_view);
    if (!shadow_render_node) {
        return;
    }

    auto&                              render_context = context.render_context;
    erhe::renderer::Primitive_renderer line_renderer  = render_context.get({erhe::graphics::Primitive_type::line, 3, true, true});
    const auto& light_projections           = shadow_render_node->get_light_projections();
    const auto* light                       = context.light;
    const auto  light_projection_transforms = light_projections.get_light_projection_transforms_for_light(light);

    if (light_projection_transforms == nullptr) {
        return;
    }

    const glm::mat4 world_from_light_clip   = light_projection_transforms->clip_from_world.get_inverse_matrix();
    const glm::mat4 world_from_light_camera = light_projection_transforms->world_from_light_camera.get_matrix();

    line_renderer.set_thickness(m_settings.light_line_width);

    if (m_settings.frustum_box) {
        line_renderer.add_cube(
            world_from_light_clip,
            context.light_color,
            clip_min_corner,
            clip_max_corner
        );
    }

    if (m_settings.frustum_planes) {
        const glm::mat4 clip_from_world = light_projection_transforms->clip_from_world.get_matrix();
        std::array<glm::vec4, 6> planes = erhe::math::extract_frustum_planes(clip_from_world, 0.0f, 1.0f);
        for (const glm::vec4& plane : planes) {
            line_renderer.add_plane(context.light_color, plane);
        }
    }

    const float projection_scale = light_projections.parameters.view_camera->get_projection_scale();
    const float r_major = light_projections.parameters.view_camera->get_shadow_range() / (40.0f * projection_scale);
    line_renderer.set_thickness(-1.0f);
    line_renderer.add_lines(
        world_from_light_camera,
        context.light_color,
        {
            { O, -1.0f * axis_z },
            { O - r_major * axis_x, O + r_major * axis_x },
            { O - r_major * axis_y, O + r_major * axis_y }
        }
    );

    const float r_minor = light_projections.parameters.view_camera->get_shadow_range() / (100.0f * projection_scale);
    line_renderer.set_thickness(-2.0f);
    std::array<erhe::renderer::Line, 7> lines;
    for (int i = 0; i < 7; ++i) {
        const float rel   = static_cast<float>(i) / 7.0f;
        const float theta = rel * glm::pi<float>();
        const float x     = r_minor * cos(theta);
        const float y     = r_minor * sin(theta);
        lines[i] = erhe::renderer::Line{O - x * axis_x - y * axis_y, O + x * axis_x + y * axis_y};
    }
    line_renderer.add_lines(
        world_from_light_camera,
        context.light_color,
        lines
    );
}

void Debug_visualizations::shadow_frustum_fit_visualization(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    // Tight shadow frustum fit intermediates; collected per light only when
    // the Shadow Frustum Fit setting "Collect Debug Data" is enabled. Each
    // element has its own toggle in the Shadow Fit group of this window.
    // Gated only by m_settings.shadow_fit_debug - independent of the lights
    // visualization mode (which gates light_visualization()) and of
    // m_settings.shadow_debug (which gates the shadow texel visualization).
    const auto shadow_render_node = context.app_context.app_rendering->get_shadow_node_for_view(context.scene_view);
    if (!shadow_render_node) {
        // Diagnostics: on the headset this means no Shadow_render_node
        // is registered for the headset Scene_view, so the fit viz cannot run.
        if (m_dbg_last_node_found) {
            m_dbg_last_node_found = false;
            log_debug_visualization->info("Shadow fit viz: no shadow render node for this view - skipping");
        }
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 3, true, true});
    const auto& light_projections = shadow_render_node->get_light_projections();

    // One-shot dump of the whole fit debug geometry (all lights) to the log,
    // triggered by the imgui() button. Consumed here so it fires once.
    if (m_shadow_fit_dump_requested) {
        m_shadow_fit_dump_requested = false;
        log_debug_visualization->info("Shadow fit debug dump:\n{}", dump_shadow_fit_debug(light_projections).dump(2));
    }

    const std::vector<erhe::scene::Light_projection_transforms>&    transforms     = light_projections.light_projection_transforms;
    const std::vector<erhe::scene::Shadow_frustum_fit_debug_data>&  fit_debug_data = light_projections.fit_debug_data;

    // The fit - and the per-caster affecting / culled classification - is per
    // light, so visualize the single light selected in imgui(). Fall back to
    // the first light with valid fit debug data when nothing is selected (or
    // the selection has no valid fit this frame).
    const std::shared_ptr<erhe::scene::Light> selected_light = m_shadow_fit_light.lock();
    const erhe::scene::Shadow_frustum_fit_debug_data* fit_debug_ptr = nullptr;
    const erhe::scene::Light*                         resolved_light = nullptr;
    const std::size_t light_count = (transforms.size() < fit_debug_data.size()) ? transforms.size() : fit_debug_data.size();
    for (std::size_t i = 0; i < light_count; ++i) {
        if (!fit_debug_data[i].valid) {
            continue;
        }
        if (selected_light && (transforms[i].light == selected_light.get())) {
            fit_debug_ptr  = &fit_debug_data[i];
            resolved_light = transforms[i].light;
            break;
        }
        if (fit_debug_ptr == nullptr) {
            fit_debug_ptr  = &fit_debug_data[i]; // first valid entry, used unless the selected light is found
            resolved_light = transforms[i].light;
        }
    }

    // Diagnostics: a one line gating summary, logged only when the boolean
    // state changes. Reveals whether the fit produced valid debug data at all
    // and the sizes the per element toggles draw from (a valid fit but empty
    // shadow_volume_planes / caster_boxes means fit_to_casters did not run).
    {
        const bool        fit_valid        = (fit_debug_ptr != nullptr) && fit_debug_ptr->valid;
        const bool        corners_valid    = (fit_debug_ptr != nullptr) && fit_debug_ptr->view_frustum_corners_valid;
        const std::size_t volume_plane_n   = (fit_debug_ptr != nullptr) ? fit_debug_ptr->shadow_volume_planes.size() : 0;
        const std::size_t caster_box_n     = (fit_debug_ptr != nullptr) ? fit_debug_ptr->caster_boxes.size()         : 0;
        // Throttle on the boolean state only (node present, fit valid, corners
        // valid). The plane / caster counts oscillate every frame as the head
        // moves, so keying on them would log every frame; they are still
        // reported in the message for the transition that does fire.
        if (
            (m_dbg_last_node_found    != true)        ||
            (m_dbg_last_fit_valid     != fit_valid)   ||
            (m_dbg_last_corners_valid != corners_valid)
        ) {
            m_dbg_last_node_found      = true;
            m_dbg_last_fit_valid       = fit_valid;
            m_dbg_last_corners_valid   = corners_valid;
            log_debug_visualization->info(
                "Shadow fit viz: transforms={} fit_debug_data={} resolved_fit_valid={} corners_valid={} volume_planes={} caster_boxes={} light='{}'",
                transforms.size(),
                fit_debug_data.size(),
                fit_valid,
                corners_valid,
                volume_plane_n,
                caster_box_n,
                (resolved_light != nullptr) ? resolved_light->get_name() : std::string{"<none>"}
            );
        }
    }

    if (fit_debug_ptr == nullptr) {
        return;
    }
    {
        const erhe::scene::Shadow_frustum_fit_debug_data& fit_debug = *fit_debug_ptr;

        // Casters: classify every visible shadow caster against the selected
        // light's shadow caster volume F_shadow (the same test the fit applies
        // to its gathered casters - erhe::math::aabb_in_convex_volume), tag the
        // mesh with Item_flags::affects_shadow, and draw it in the affecting or
        // culled color. shadow_volume_planes is empty when fit_to_casters did
        // not run, in which case there is nothing to classify against.
        if (m_settings.shadow_fit_casters && !fit_debug.shadow_volume_planes.empty()) {
            const std::shared_ptr<Scene_root> scene_root = context.scene_view.get_scene_root();
            if (scene_root) {
                line_renderer.set_thickness(m_settings.shadow_fit_casters_width);
                // Matches Shadow_renderer's shadow_filter (visible + shadow_cast).
                const uint64_t require_bits = erhe::Item_flags::visible | erhe::Item_flags::shadow_cast;
                for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
                    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : layer->meshes) {
                        if (!mesh || ((mesh->get_flag_bits() & require_bits) != require_bits)) {
                            continue;
                        }
                        const erhe::math::Aabb aabb = mesh->get_aabb_world();
                        if (!aabb.is_valid()) {
                            continue;
                        }
                        const bool affects = erhe::math::aabb_in_convex_volume(fit_debug.shadow_volume_planes, aabb);
                        mesh->set_flag_bits(erhe::Item_flags::affects_shadow, affects);
                        line_renderer.add_cube(
                            glm::mat4{1.0f},
                            affects ? m_settings.shadow_fit_casters_color : m_settings.shadow_fit_casters_culled_color,
                            aabb.min,
                            aabb.max
                        );
                    }
                }
            }
        }

        // Draws a convex hull as its unique triangle edges (each undirected
        // edge appears twice in the triangle list; the index-order tests keep
        // one of the two).
        const auto draw_hull_edges = [&line_renderer](const erhe::math::Convex_hull& hull, const glm::vec4& color, const float width) {
            for (const std::array<std::size_t, 3>& triangle : hull.triangle_indices) {
                const std::size_t i0 = triangle[0];
                const std::size_t i1 = triangle[1];
                const std::size_t i2 = triangle[2];
                if (i0 < i1) {
                    line_renderer.add_line(color, width, hull.points[i0], color, width, hull.points[i1]);
                }
                if (i1 < i2) {
                    line_renderer.add_line(color, width, hull.points[i1], color, width, hull.points[i2]);
                }
                if (i2 < i0) {
                    line_renderer.add_line(color, width, hull.points[i2], color, width, hull.points[i0]);
                }
            }
        };

        // Convex hull around the caster bounds, before clipping to F_shadow
        if (m_settings.shadow_fit_caster_hull) {
            draw_hull_edges(fit_debug.caster_hull, m_settings.shadow_fit_caster_hull_color, m_settings.shadow_fit_caster_hull_width);
        }

        // Step 1: view frustum (F_main, truncated to the shadow range) - the
        // region shadow receivers are filtered against.
        if (m_settings.shadow_fit_view_frustum && fit_debug.view_frustum_corners_valid) {
            const glm::vec4 color = m_settings.shadow_fit_view_frustum_color;
            const float     width = m_settings.shadow_fit_view_frustum_width;
            const std::array<glm::vec3, 8>& c = fit_debug.view_frustum_corners;
            // Corner order from erhe::math::extract_frustum_corners()
            constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
                {0, 1}, {1, 3}, {3, 2}, {2, 0}, // near rectangle
                {4, 5}, {5, 7}, {7, 6}, {6, 4}, // far rectangle
                {0, 4}, {1, 5}, {2, 6}, {3, 7}  // connecting edges
            }};
            for (const std::array<std::size_t, 2>& edge : edges) {
                line_renderer.add_line(color, width, c[edge[0]], color, width, c[edge[1]]);
            }
        }

        // Step 2: shadow receivers - every visible content-mesh world AABB,
        // colored by whether it passes the view frustum filter (and so
        // contributes to the receiver-aware caster cull, fit_to_receivers) or
        // fails it. This is the same erhe::math::aabb_in_frustum filter
        // build_receiver_cull_planes applies, evaluated here independent of
        // whether fit_to_receivers is enabled.
        if (m_settings.shadow_fit_receivers && fit_debug.view_frustum_corners_valid) {
            const std::shared_ptr<Scene_root> scene_root = context.scene_view.get_scene_root();
            if (scene_root) {
                erhe::scene::Mesh_layer* const content_layer = scene_root->layers().content();
                if (content_layer != nullptr) {
                    line_renderer.set_thickness(m_settings.shadow_fit_receivers_width);
                    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
                        if (!mesh || ((mesh->get_flag_bits() & erhe::Item_flags::visible) == 0)) {
                            continue;
                        }
                        const erhe::math::Aabb aabb = mesh->get_aabb_world();
                        if (!aabb.is_valid()) {
                            continue;
                        }
                        const bool passes = erhe::math::aabb_in_frustum(
                            fit_debug.view_frustum_planes, fit_debug.view_frustum_corners, aabb
                        );
                        line_renderer.add_cube(
                            glm::mat4{1.0f},
                            passes ? m_settings.shadow_fit_receivers_color : m_settings.shadow_fit_receivers_culled_color,
                            aabb.min,
                            aabb.max
                        );
                    }
                }
            }
        }

        // Receiver hull: convex hull of the receivers that intersect the view
        // frustum - the body extruded toward the light into the caster cull
        // volume (the receiver analogue of the caster hull). Drawn unclipped
        // (the raw receiver cluster hull) and/or clipped to the view frustum
        // (mirroring build_receiver_cull_planes: receivers only matter where
        // they are actually visible). Built from the same passing receivers as
        // the receiver boxes above, independent of whether fit_to_receivers is
        // enabled.
        if ((m_settings.shadow_fit_receiver_hull || m_settings.shadow_fit_receiver_hull_unclipped) && fit_debug.view_frustum_corners_valid) {
            const std::shared_ptr<Scene_root> scene_root = context.scene_view.get_scene_root();
            if (scene_root) {
                erhe::scene::Mesh_layer* const content_layer = scene_root->layers().content();
                if (content_layer != nullptr) {
                    std::vector<glm::vec3> receiver_points;
                    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
                        if (!mesh || ((mesh->get_flag_bits() & erhe::Item_flags::visible) == 0)) {
                            continue;
                        }
                        const erhe::math::Aabb aabb = mesh->get_aabb_world();
                        if (!aabb.is_valid() ||
                            !erhe::math::aabb_in_frustum(fit_debug.view_frustum_planes, fit_debug.view_frustum_corners, aabb)) {
                            continue;
                        }
                        const glm::vec3& a = aabb.min;
                        const glm::vec3& b = aabb.max;
                        receiver_points.push_back(glm::vec3{a.x, a.y, a.z});
                        receiver_points.push_back(glm::vec3{a.x, a.y, b.z});
                        receiver_points.push_back(glm::vec3{a.x, b.y, a.z});
                        receiver_points.push_back(glm::vec3{a.x, b.y, b.z});
                        receiver_points.push_back(glm::vec3{b.x, a.y, a.z});
                        receiver_points.push_back(glm::vec3{b.x, a.y, b.z});
                        receiver_points.push_back(glm::vec3{b.x, b.y, a.z});
                        receiver_points.push_back(glm::vec3{b.x, b.y, b.z});
                    }
                    if (receiver_points.size() >= 4) {
                        const erhe::math::Convex_hull hull = erhe::math::calculate_bounding_convex_hull(receiver_points);
                        if (m_settings.shadow_fit_receiver_hull_unclipped) {
                            draw_hull_edges(hull, m_settings.shadow_fit_receiver_hull_unclipped_color, m_settings.shadow_fit_receiver_hull_unclipped_width);
                        }
                        if (m_settings.shadow_fit_receiver_hull) {
                            // Clip the receiver hull to the view frustum.
                            // clip_convex_hull_points_to_frustum computes the
                            // complete convex intersection, then the clipped
                            // point set is re-hulled.
                            std::vector<glm::vec3> clipped = erhe::math::clip_convex_hull_points_to_frustum(hull, fit_debug.view_frustum_planes, fit_debug.view_frustum_corners);
                            if (clipped.size() >= 4) {
                                const erhe::math::Convex_hull clipped_hull = erhe::math::calculate_bounding_convex_hull(clipped);
                                draw_hull_edges(clipped_hull, m_settings.shadow_fit_receiver_hull_color, m_settings.shadow_fit_receiver_hull_width);
                            }
                        }
                    }
                }
            }
        }

        // Caster-cull volume planes: the convex polyhedron those half-spaces
        // bound. The volume is open toward the light (its light-facing cap is
        // the one missing plane), so a synthetic cap one shadow range beyond
        // the frustum toward the light is added only to bound it for display;
        // the cap's own orientation indicator is not drawn. Drawn as the
        // polyhedron's intersection edges and vertices, plus an add_plane()
        // orientation indicator at the center of each real plane's face.
        if (m_settings.shadow_fit_volume_planes &&
            !fit_debug.shadow_volume_planes.empty() &&
            fit_debug.view_frustum_corners_valid &&
            (resolved_light != nullptr) &&
            (resolved_light->get_node() != nullptr))
        {
            const glm::vec4 color = m_settings.shadow_fit_volume_planes_color;
            const float     width = m_settings.shadow_fit_volume_planes_width;

            const glm::vec3 light_direction = glm::normalize(glm::vec3{resolved_light->get_node()->direction_in_world()});
            const float     shadow_range    = light_projections.parameters.view_camera->get_shadow_range();
            float s_max = glm::dot(fit_debug.view_frustum_corners[0], light_direction);
            for (const glm::vec3& corner : fit_debug.view_frustum_corners) {
                const float s = glm::dot(corner, light_direction);
                if (s > s_max) {
                    s_max = s;
                }
            }

            const std::size_t      real_plane_count = fit_debug.shadow_volume_planes.size();
            std::vector<glm::vec4> capped_planes    = fit_debug.shadow_volume_planes;
            capped_planes.push_back(glm::vec4{-light_direction, s_max + shadow_range}); // synthetic cap

            const erhe::math::Convex_polyhedron polyhedron = erhe::math::convex_polyhedron_from_planes(capped_planes);

            line_renderer.set_thickness(width);
            for (const std::array<std::size_t, 2>& edge : polyhedron.edges) {
                line_renderer.add_line(color, width, polyhedron.vertices[edge[0]], color, width, polyhedron.vertices[edge[1]]);
            }

            const float marker = 0.05f * shadow_range;
            for (const glm::vec3& v : polyhedron.vertices) {
                line_renderer.add_line(color, width, v - marker * axis_x, color, width, v + marker * axis_x);
                line_renderer.add_line(color, width, v - marker * axis_y, color, width, v + marker * axis_y);
                line_renderer.add_line(color, width, v - marker * axis_z, color, width, v + marker * axis_z);
            }

            // Orientation indicator at each real plane's face center (skip the
            // synthetic cap, the last plane).
            for (std::size_t p = 0; (p < real_plane_count) && (p < polyhedron.face_vertices.size()); ++p) {
                const std::vector<std::size_t>& face = polyhedron.face_vertices[p];
                if (face.size() < 3) {
                    continue;
                }
                glm::vec3 centroid{0.0f};
                for (const std::size_t vi : face) {
                    centroid += polyhedron.vertices[vi];
                }
                centroid /= static_cast<float>(face.size());
                line_renderer.add_plane_indicator(color, centroid, glm::vec3{fit_debug.shadow_volume_planes[p]});
            }
        }

        // Far plane hull: the receiver silhouette (the 2D hull of the clipped
        // receiver hull projected along the light onto the flat far cap, lifted
        // back to 3D) the receiver caster-cull volume side planes are swept from.
        // Drawn directly from the stored polygon (not via
        // convex_polyhedron_from_planes), so it shows the silhouette the fit
        // actually built, independent of the volume reconstruction.
        if (m_settings.shadow_fit_far_plane_hull && (fit_debug.receiver_far_plane_hull.size() >= 2)) {
            const glm::vec4   color = m_settings.shadow_fit_far_plane_hull_color;
            const float       width = m_settings.shadow_fit_far_plane_hull_width;
            const std::size_t count = fit_debug.receiver_far_plane_hull.size();
            for (std::size_t i = 0; i < count; ++i) {
                line_renderer.add_line(
                    color, width, fit_debug.receiver_far_plane_hull[i],
                    color, width, fit_debug.receiver_far_plane_hull[(i + 1) % count]
                );
            }
        }

        // Fit point set (clipped caster hull points or view frustum corners)
        if (m_settings.shadow_fit_points) {
            const glm::vec4 color = m_settings.shadow_fit_points_color;
            const float     width = m_settings.shadow_fit_points_width;
            const float marker_radius = 0.05f;
            for (const glm::vec3& p : fit_debug.fit_points) {
                line_renderer.add_line(color, width, p - marker_radius * axis_x, color, width, p + marker_radius * axis_x);
                line_renderer.add_line(color, width, p - marker_radius * axis_y, color, width, p + marker_radius * axis_y);
                line_renderer.add_line(color, width, p - marker_radius * axis_z, color, width, p + marker_radius * axis_z);
            }
        }

        // Light plane hull, minimum area OBB and its supporting edge
        // (present when Optimize Rotation is enabled)
        if (m_settings.shadow_fit_light_plane_hull) {
            const glm::mat4& world_from_light_plane = fit_debug.world_from_light_plane;
            auto to_world = [&world_from_light_plane](const glm::vec2 p) -> glm::vec3 {
                return glm::vec3{world_from_light_plane * glm::vec4{p.x, p.y, 0.0f, 1.0f}};
            };
            const glm::vec4 hull_color = m_settings.shadow_fit_light_plane_hull_color;
            const float     hull_width = m_settings.shadow_fit_light_plane_hull_width;
            for (std::size_t i = 0, count = fit_debug.light_plane_hull.size(); i < count; ++i) {
                line_renderer.add_line(
                    hull_color, hull_width, to_world(fit_debug.light_plane_hull[i]),
                    hull_color, hull_width, to_world(fit_debug.light_plane_hull[(i + 1) % count])
                );
            }
            if (!fit_debug.light_plane_hull.empty()) {
                const erhe::math::Min_area_obb_2d& obb = fit_debug.obb;
                const glm::mat2 original_from_edge = glm::transpose(obb.edge_from_original);
                const std::array<glm::vec3, 4> obb_corners{
                    to_world(original_from_edge * glm::vec2{obb.aabb_min.x, obb.aabb_min.y}),
                    to_world(original_from_edge * glm::vec2{obb.aabb_max.x, obb.aabb_min.y}),
                    to_world(original_from_edge * glm::vec2{obb.aabb_max.x, obb.aabb_max.y}),
                    to_world(original_from_edge * glm::vec2{obb.aabb_min.x, obb.aabb_max.y})
                };
                const glm::vec4 obb_color = m_settings.shadow_fit_obb_color;
                const float     obb_width = m_settings.shadow_fit_obb_width;
                for (std::size_t i = 0, count = obb_corners.size(); i < count; ++i) {
                    line_renderer.add_line(obb_color, obb_width, obb_corners[i], obb_color, obb_width, obb_corners[(i + 1) % count]);
                }
                line_renderer.add_line(
                    m_settings.shadow_fit_obb_edge_color, m_settings.shadow_fit_obb_edge_width, to_world(obb.edge_a),
                    m_settings.shadow_fit_obb_edge_color, m_settings.shadow_fit_obb_edge_width, to_world(obb.edge_b)
                );
            }
        }

        // Light space box after each pipeline step, in the fit frame
        {
            using erhe::scene::Shadow_fit_step;
            class Step_visualization
            {
            public:
                Shadow_fit_step step;
                bool            enabled;
                glm::vec4       color;
            };
            const std::array<Step_visualization, 4> step_visualizations{{
                { Shadow_fit_step::fit_points,         m_settings.shadow_fit_box_fit,     m_settings.shadow_fit_box_fit_color     },
                { Shadow_fit_step::frustum_constraint, m_settings.shadow_fit_box_frustum, m_settings.shadow_fit_box_frustum_color },
                { Shadow_fit_step::shadow_range_cap,   m_settings.shadow_fit_box_cap,     m_settings.shadow_fit_box_cap_color     },
                { Shadow_fit_step::stabilized,         m_settings.shadow_fit_box_final,   m_settings.shadow_fit_box_final_color   }
            }};
            line_renderer.set_thickness(m_settings.shadow_fit_box_width);
            for (const Step_visualization& v : step_visualizations) {
                const auto& box = fit_debug.step_boxes[static_cast<std::size_t>(v.step)];
                if (!v.enabled || !box.valid) {
                    continue;
                }
                line_renderer.add_cube(fit_debug.world_from_fit_frame, v.color, box.min, box.max);
            }
        }
    }
}

void Debug_visualizations::point_light_visualization(const Light_visualization_context& context)
{
    const auto* node = context.light->get_node();
    if (node == nullptr) {
        return;
    }
    auto&                              render_context = context.render_context;
    erhe::renderer::Primitive_renderer line_renderer  = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    constexpr float scale = 0.5f;
    const auto nnn = scale * glm::normalize(-axis_x - axis_y - axis_z);
    const auto nnp = scale * glm::normalize(-axis_x - axis_y + axis_z);
    const auto npn = scale * glm::normalize(-axis_x + axis_y - axis_z);
    const auto npp = scale * glm::normalize(-axis_x + axis_y + axis_z);
    const auto pnn = scale * glm::normalize( axis_x - axis_y - axis_z);
    const auto pnp = scale * glm::normalize( axis_x - axis_y + axis_z);
    const auto ppn = scale * glm::normalize( axis_x + axis_y - axis_z);
    const auto ppp = scale * glm::normalize( axis_x + axis_y + axis_z);
    line_renderer.set_thickness(m_settings.light_line_width);
    line_renderer.add_lines(
        node->world_from_node(),
        context.light_color,
        {
            { -scale * axis_x, scale * axis_x},
            { -scale * axis_y, scale * axis_y},
            { -scale * axis_z, scale * axis_z},
            { nnn, ppp },
            { nnp, ppn },
            { npn, pnp },
            { npp, pnn }
        }
    );
}

void Debug_visualizations::spot_light_visualization(const Light_visualization_context& context)
{
    const auto* node = context.light->get_node();
    if (node == nullptr) {
        return;
    }

    const auto* camera_node = context.render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = context.render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    const erhe::scene::Light* light = context.light;

    constexpr int   edge_count       = 200;
    constexpr float light_cone_sides = edge_count * 6;
    const float outer_alpha   = light->outer_spot_angle;
    const float inner_alpha   = light->inner_spot_angle;
    const float length        = light->range;
    const float outer_apothem = length * std::tan(outer_alpha * 0.5f);
    const float inner_apothem = length * std::tan(inner_alpha * 0.5f);
    const float outer_radius  = outer_apothem / std::cos(glm::pi<float>() / static_cast<float>(light_cone_sides));
    const float inner_radius  = inner_apothem / std::cos(glm::pi<float>() / static_cast<float>(light_cone_sides));

    const mat4 m             = node->world_from_node();
    const vec3 view_position = node->transform_point_from_world_to_local(
        camera_node->position_in_world()
    );

    //auto* app_time = get<App_time>();
    //const float time = static_cast<float>(app_time->time());
    //const float half_position = (app_time != nullptr)
    //    ? time - floor(time)
    //    : 0.5f;

    line_renderer.set_thickness(m_settings.light_line_width);

    for (int i = 0; i < light_cone_sides; ++i) {
        const float t0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(light_cone_sides);
        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(light_cone_sides);
        line_renderer.add_lines(
            m,
            context.light_color,
            {
                {
                    -length * axis_z
                    + outer_radius * std::cos(t0) * axis_x
                    + outer_radius * std::sin(t0) * axis_y,
                    -length * axis_z
                    + outer_radius * std::cos(t1) * axis_x
                    + outer_radius * std::sin(t1) * axis_y
                }
            }
        );
        line_renderer.add_lines(
            m,
            context.half_light_color,
            {
                {
                    -length * axis_z + inner_radius * std::cos(t0) * axis_x + inner_radius * std::sin(t0) * axis_y,
                    -length * axis_z + inner_radius * std::cos(t1) * axis_x + inner_radius * std::sin(t1) * axis_y
                }
                //{
                //    -length * axis_z * half_position
                //    + outer_radius * std::cos(t0) * axis_x * half_position
                //    + outer_radius * std::sin(t0) * axis_y * half_position,
                //    -length * axis_z * half_position
                //    + outer_radius * std::cos(t1) * axis_x * half_position
                //    + outer_radius * std::sin(t1) * axis_y * half_position
                //}
            }
        );
    }
    line_renderer.add_lines(
        m,
        context.half_light_color,
        {
            //{
            //    O,
            //    -length * axis_z - outer_radius * axis_x,
            //},
            //{
            //    O,
            //    -length * axis_z + outer_radius * axis_x
            //},
            //{
            //    O,
            //    -length * axis_z - outer_radius * axis_y,
            //},
            //{
            //    O,
            //    -length * axis_z + outer_radius * axis_y
            //},
            {
                O,
                -length * axis_z
            },
            {
                -length * axis_z - inner_radius * axis_x,
                -length * axis_z + inner_radius * axis_x
            },
            {
                -length * axis_z - inner_radius * axis_y,
                -length * axis_z + inner_radius * axis_y
            }
        }
    );

    class Cone_edge
    {
    public:
        Cone_edge(
            const vec3& p,
            const vec3& n,
            const vec3& t,
            const vec3& b,
            const float phi,
            const float n_dot_v
        )
        : p      {p}
        , n      {n}
        , t      {t}
        , b      {b}
        , phi    {phi}
        , n_dot_v{n_dot_v}
        {
        }

        vec3  p;
        vec3  n;
        vec3  t;
        vec3  b;
        float phi;
        float n_dot_v;
    };

    std::vector<Cone_edge> cone_edges;
    for (int i = 0; i < edge_count; ++i) {
        const float phi     = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(edge_count);
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);

        const vec3 p{
            + outer_radius * sin_phi,
            + outer_radius * cos_phi,
            -length
        };

        const vec3 B = normalize(O - p); // generatrix
        const vec3 T{
            static_cast<float>(std::sin(phi + glm::half_pi<float>())),
            static_cast<float>(std::cos(phi + glm::half_pi<float>())),
            0.0f
        };
        const vec3  N0      = glm::cross(B, T);
        const vec3  N       = glm::normalize(N0);
        const vec3  v       = normalize(p - view_position);
        const float n_dot_v = dot(N, v);

        cone_edges.emplace_back(p, N, T, B, phi, n_dot_v);
    }

    std::vector<Cone_edge> sign_flip_edges;
    for (size_t i = 0; i < cone_edges.size(); ++i) {
        const std::size_t next_i    = (i + 1) % cone_edges.size();
        const auto&       edge      = cone_edges[i];
        const auto&       next_edge = cone_edges[next_i];
        if (sign(edge.n_dot_v) != sign(next_edge.n_dot_v)) {
            if (std::abs(edge.n_dot_v) < std::abs(next_edge.n_dot_v)) {
                sign_flip_edges.push_back(edge);
            } else {
                sign_flip_edges.push_back(next_edge);
            }
        }
    }

    for (auto& edge : sign_flip_edges) {
        line_renderer.add_lines(m, context.light_color, { { O, edge.p } } );
        //line_renderer.add_lines(m, red,   { { edge.p, edge.p + 0.1f * edge.n } }, thickness);
        //line_renderer.add_lines(m, green, { { edge.p, edge.p + 0.1f * edge.t } }, thickness);
        //line_renderer.add_lines(m, blue,  { { edge.p, edge.p + 0.1f * edge.b } }, thickness);
    }
}

void Debug_visualizations::camera_visualization(const Render_context& render_context, const erhe::scene::Camera* camera)
{
    ERHE_PROFILE_FUNCTION();

    if (camera == render_context.camera) {
        return;
    }

    using namespace erhe::utility;
    if (!test_bit_set(camera->get_flag_bits(), erhe::Item_flags::show_debug_visualizations)) {
        return;
    }

    const auto* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        return;
    }

    const auto& view_camera = render_context.scene_view.get_camera();
    if (!view_camera) {
        return;
    }
    const auto* view_camera_node = view_camera->get_node();
    if (view_camera_node == nullptr) {
        return;
    }
    const erhe::math::Depth_range            depth_range = render_context.scene_view.get_depth_range();
    const erhe::math::Coordinate_conventions conventions = render_context.scene_view.get_conventions();
    const bool  reverse_depth        = render_context.scene_view.get_reverse_depth();
    const float aspect_ratio         = render_context.viewport.aspect_ratio();
    const mat4  view_clip_from_node  = view_camera->projection()->get_projection_matrix(aspect_ratio, reverse_depth, depth_range, conventions);
    const mat4  view_clip_from_world = view_clip_from_node * view_camera_node->node_from_world();
    const mat4  clip_from_node       = camera->projection()->get_projection_matrix(
        1.0f,
        reverse_depth,
        depth_range,
        conventions
    );
    const mat4 clip_from_world = clip_from_node * camera_node->node_from_world();
    const mat4 node_from_clip  = inverse(clip_from_node);
    const mat4 world_from_clip = camera_node->world_from_node() * node_from_clip;

    erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    line_renderer.set_thickness(m_settings.camera_line_width);

    std::array<glm::vec4, 6> planes  = erhe::math::extract_frustum_planes (clip_from_world, 0.0f, 1.0f);
    std::array<glm::vec3, 8> corners = erhe::math::extract_frustum_corners(world_from_clip, 0.0f, 1.0f);
    {
        const std::array<glm::vec3, 8>& p = corners;
        line_renderer.add_lines(
            glm::mat4{1.0f}, //erhe::scene::Transform{},
            glm::vec4{1.0f, 0.0, 0.0f, 1.0f},
            {
                // See extract_frustum_corners() for clip space corner numbering scheme
                // near plane
                { p[0], p[1] },
                { p[1], p[3] },
                { p[3], p[2] },
                { p[2], p[0] },

                // far plane
                { p[4], p[5] },
                { p[5], p[7] },
                { p[7], p[6] },
                { p[6], p[4] },

                // near to far
                { p[0], p[4] },
                { p[1], p[5] },
                { p[2], p[6] },
                { p[3], p[7] }
            }
        );
    }
    for (size_t i = 0, end = corners.size(); i < end; ++i) {
        const glm::vec3 p          = corners[i];
        const auto      p_window   = render_context.viewport.project_to_screen_space(view_clip_from_world, p, 0.0f, 1.0f, render_context.scene_view.get_conventions());
        const uint32_t  text_color = 0xffffccaau;
        const glm::vec3 point_in_window_z_negated{p_window.x, p_window.y, -p_window.z};
        render_context.app_context.text_renderer->print(point_in_window_z_negated, text_color, fmt::format("{}", i));
    }

    if (m_settings.frustum_box) {
        line_renderer.add_cube(
            world_from_clip,
            m_settings.camera_line_color,
            clip_min_corner,
            clip_max_corner,
            true
        );
    }

    if (m_settings.frustum_planes) {
        line_renderer.add_plane(glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}, planes[0]); // R Left
        line_renderer.add_plane(glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}, planes[1]); // G Right
        line_renderer.add_plane(glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}, planes[2]); // B Bottom
        line_renderer.add_plane(glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, planes[3]); // C Top
        line_renderer.add_plane(glm::vec4{1.0f, 1.0f, 0.0f, 1.0f}, planes[4]); // Y Near
        line_renderer.add_plane(glm::vec4{1.0f, 0.0f, 1.0f, 1.0f}, planes[5]); // M Far
    }

    if (m_settings.camera_cull_test) {
        const auto scene_root = render_context.scene_view.get_scene_root();
        if (!scene_root) {
            return;
        }

        const glm::vec4 red       {1.0f, 0.0f, 0.0f, 1.0f};
        const glm::vec4 half_red  {0.5f, 0.0f, 0.0f, 0.5f};
        const glm::vec4 green     {0.0f, 1.0f, 0.0f, 1.0f};
        const glm::vec4 half_green{0.0f, 0.5f, 0.0f, 0.5f};

        line_renderer.set_thickness(4.0f);
        for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
            for (const auto& mesh : layer->meshes) {
                using namespace erhe::utility;
                if (!test_bit_set(mesh->get_flag_bits(), erhe::Item_flags::content)) {
                    continue;
                }

                for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                    const erhe::primitive::Primitive& primitive                 = *mesh_primitive.primitive.get();
                    erhe::scene::Node*                node                      = mesh->get_node();
                    const erhe::scene::Trs_transform& world_from_node_transform = node->world_from_node_transform();
                    const glm::mat4                   world_from_node           = world_from_node_transform.get_matrix();
                    const erhe::math::Aabb            node_local_aabb           = primitive.render_shape->get_renderable_mesh().bounding_box;
                    const erhe::math::Sphere          node_local_sphere         = primitive.render_shape->get_renderable_mesh().bounding_sphere;
                    const erhe::math::Aabb            aabb_in_world             = node_local_aabb  .transformed_by(world_from_node);
                    const erhe::math::Sphere          sphere_in_world           = node_local_sphere.transformed_by(world_from_node);
                    const bool aabb_visible   = aabb_in_frustum  (planes, corners, aabb_in_world);
                    const bool sphere_visible = sphere_in_frustum(planes, sphere_in_world);
                    line_renderer.add_cube(glm::mat4{1.0f}, aabb_visible ? green : red, aabb_in_world.min, aabb_in_world.max);
                    line_renderer.add_sphere(
                        erhe::scene::Transform{},
                        sphere_visible ? green      : red,
                        sphere_visible ? half_green : half_red,
                        4.0f,
                        3.0f,
                        sphere_in_world.center,
                        sphere_in_world.radius,
                        &(render_context.get_camera_node()->world_from_node_transform()),
                        m_settings.sphere_step_count
                    );
                }
            }
        }
    }
}

void Debug_visualizations::layout_visualization(const Render_context& render_context, const erhe::scene::Node& node, const erhe::scene::Layout& layout)
{
    ERHE_PROFILE_FUNCTION();

    using namespace erhe::utility;
    if (!test_bit_set(layout.get_flag_bits(), erhe::Item_flags::show_debug_visualizations)) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    line_renderer.set_thickness(m_settings.layout_line_width);
    line_renderer.add_cube(
        node.world_from_node(),
        m_settings.layout_line_color,
        layout.volume.min,
        layout.volume.max
    );
}

void Debug_visualizations::selection_visualization(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    const auto* scene = context.get_scene();
    if ((scene == nullptr) || (context.camera == nullptr)) {
        return;
    }

    const auto& viewport_config = context.viewport_scene_view->get_config();
    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    const auto& selection = context.app_context.selection->get_selected_items();

    m_selection_bounding_volume.reset();
    for (const auto& item : selection) {
        const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (node) {
            if (node->get_scene() != scene) {
                continue;
            }
            if (m_settings.node_axises == Visualization_mode::selected) {
                const glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
                const glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
                const glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
                const mat4 m{node->world_from_node()};
                line_renderer.set_thickness(m_selection_node_axis_width);
                line_renderer.add_lines( m, red,   {{ O, axis_x }} );
                line_renderer.add_lines( m, green, {{ O, axis_y }} );
                line_renderer.add_lines( m, blue,  {{ O, axis_z }} );
            }
            for (const auto& attachment : node->get_attachments()) {
                const auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(attachment);
                if (mesh) {
                    mesh_visualization(context, mesh.get());
                }
                //const auto skin = as_skin(attachment);
                //if (skin) {
                //    skin_visualization(context, skin.get());
                //}

                const auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(attachment);
                if (
                    camera &&
                    (viewport_config.debug_visualizations.camera == erhe::renderer::Visualization_mode::selected)
                ) {
                    camera_visualization(context, camera.get());
                }
            }
        }
    }

    if ((m_selection_bounding_volume.get_element_count() > 1) || !m_settings.selection_parts) {
        if (m_settings.selection_bounding_points) {
            const erhe::scene::Camera* camera = context.camera;
            const auto projection_transforms = camera->projection_transforms(
                context.viewport,
                context.scene_view.get_reverse_depth(),
                context.scene_view.get_depth_range(),
                context.scene_view.get_conventions()
            );
            const glm::mat4 clip_from_world = projection_transforms.clip_from_world.get_matrix();

            for (std::size_t i = 0, i_end = m_selection_bounding_volume.get_element_count(); i < i_end; ++i) {
                for (std::size_t j = 0, j_end = m_selection_bounding_volume.get_element_point_count(i); j < j_end; ++j) {
                    const auto point = m_selection_bounding_volume.get_point(i, j);
                    if (!point.has_value()) {
                        continue;
                    }

                    const auto point_in_window = context.viewport.project_to_screen_space(
                        clip_from_world,
                        point.value(),
                        0.0f,
                        1.0f,
                        context.scene_view.get_conventions()
                    );
                    const uint32_t  text_color = 0xff88ff88u;
                    const glm::vec3 point_in_window_z_negated{
                         point_in_window.x,
                         point_in_window.y,
                        -point_in_window.z
                    };
                    context.app_context.text_renderer->print(
                        point_in_window_z_negated,
                        text_color,
                        fmt::format("{}.{}", i, j)
                    );
                }
            }
        }

        erhe::math::Aabb   selection_bounding_box;
        erhe::math::Sphere selection_bounding_sphere;
        erhe::math::calculate_bounding_volume(m_selection_bounding_volume, selection_bounding_box, selection_bounding_sphere);
        const float box_volume    = selection_bounding_box.volume();
        const float sphere_volume = selection_bounding_sphere.volume();
        if (
            m_settings.selection_box ||
            (
                m_settings.selection_parts &&
                (
                    !m_settings.selection_sphere &&
                    (box_volume > 0.0f) &&
                    (box_volume < sphere_volume)
                )
            )
        ) {
            line_renderer.set_thickness(m_settings.selection_major_width);
            line_renderer.add_cube(
                glm::mat4{1.0f},
                m_settings.group_selection_major_color,
                selection_bounding_box.min - glm::vec3{m_settings.gap, m_settings.gap, m_settings.gap},
                selection_bounding_box.max + glm::vec3{m_settings.gap, m_settings.gap, m_settings.gap}
            );
        }
        if (
            m_settings.selection_sphere ||
            (
                m_settings.selection_parts &&
                (
                    !m_settings.selection_box &&
                    (sphere_volume > 0.0f) &&
                    (sphere_volume < box_volume)
                )
            )
        ) {
            const auto* camera_node = context.get_camera_node();
            if (camera_node != nullptr) {
                line_renderer.add_sphere(
                    erhe::scene::Transform{},
                    m_settings.group_selection_major_color,
                    m_settings.group_selection_minor_color,
                    m_settings.selection_major_width,
                    m_settings.selection_minor_width,
                    selection_bounding_sphere.center,
                    selection_bounding_sphere.radius + m_settings.gap,
                    &(camera_node->world_from_node_transform()),
                    m_settings.sphere_step_count
                );
            }
        }
    }

    if (m_settings.selection_convex_hull || m_settings.selection_convex_hull_projected) {
        erhe::math::Convex_hull selection_convex_hull = erhe::math::calculate_bounding_convex_hull(m_selection_bounding_volume);
        if (m_settings.selection_convex_hull) {
            for (const std::array<std::size_t, 3>& triangle : selection_convex_hull.triangle_indices) {
                const std::size_t i0 = triangle[0];
                const std::size_t i1 = triangle[1];
                const std::size_t i2 = triangle[2];
                const glm::vec3   p0 = selection_convex_hull.points[i0];
                const glm::vec3   p1 = selection_convex_hull.points[i1];
                const glm::vec3   p2 = selection_convex_hull.points[i2];
                if (i0 < i1) {
                    line_renderer.add_line(
                        m_settings.group_selection_major_color,
                        m_settings.selection_major_width,
                        p0,
                        m_settings.group_selection_major_color,
                        m_settings.selection_major_width,
                        p1
                    );
                }
                if (i1 < i2) {
                    line_renderer.add_line(
                        m_settings.group_selection_major_color,
                        m_settings.selection_major_width,
                        p1,
                        m_settings.group_selection_major_color,
                        m_settings.selection_major_width,
                        p2
                    );
                }
                if (i2 < i0) {
                    line_renderer.add_line(
                        m_settings.group_selection_major_color,
                        m_settings.selection_major_width,
                        p2,
                        m_settings.group_selection_major_color,
                        m_settings.selection_major_width,
                        p0
                    );
                }
            }
        }
        std::shared_ptr<erhe::scene::Camera> selected_camera = get_selected_camera(context);
        if (m_settings.selection_convex_hull_projected && selected_camera) {
            erhe::scene::Node* camera_node     = selected_camera->get_node();
            const mat4         clip_from_node  = selected_camera->projection()->get_projection_matrix(
                1.0f,
                context.scene_view.get_reverse_depth(),
                context.scene_view.get_depth_range(),
                context.scene_view.get_conventions()
            );
            const mat4         clip_from_world = clip_from_node * camera_node->node_from_world();
            const mat4         node_from_clip  = inverse(clip_from_node);
            const mat4         world_from_clip = camera_node->world_from_node() * node_from_clip;

            std::vector<glm::vec2> ndc_points;
            for (const glm::vec3& p : selection_convex_hull.points) {
                glm::vec4 p_in_clip = clip_from_world * glm::vec4(p, 1.0f);
                glm::vec2 p_in_ndc  = glm::vec3{p_in_clip} / p_in_clip.w;
                ndc_points.push_back(p_in_ndc);
            }

            auto unproject_nearplane_to_world = [&](glm::vec2 p_in_ndc, float depth = 1.0f) -> vec3
            {
                glm::vec4 p_in_ndc_near = glm::vec4(p_in_ndc.x, p_in_ndc.y, depth, 1.0f);
                glm::vec4 world_pos_h   = world_from_clip * p_in_ndc_near;
                glm::vec3 world_pos     = glm::vec3{world_pos_h} / world_pos_h.w;
                return world_pos;
            };

            {
                std::vector<glm::vec3> projected_convex_hull_points;
                std::vector<glm::vec2> ndc_convex_hull = erhe::math::calculate_bounding_convex_hull(ndc_points);
                for (const glm::vec2& p_in_ndc : ndc_convex_hull) {
                    glm::vec3 world_pos = unproject_nearplane_to_world(p_in_ndc);
                    projected_convex_hull_points.push_back(world_pos);
                }

                const glm::vec4 magenta{1.0f, 0.0f, 1.0f, 0.7f};
                for (std::size_t i = 0, count = projected_convex_hull_points.size(); i < count; ++i) {
                    line_renderer.add_line(
                        magenta, 2.0f, projected_convex_hull_points[i              ],
                        magenta, 2.0f, projected_convex_hull_points[(i + 1) % count]
                    );
                }

                const erhe::math::Min_area_obb_2d obb = erhe::math::calculate_min_area_obb_2d(
                    ndc_convex_hull,
                    m_settings.debug_convex_hull ? m_settings.convex_hull_edge : -1
                );
                const glm::vec2 edge_aabb_min      = obb.aabb_min;
                const glm::vec2 edge_aabb_max      = obb.aabb_max;
                const glm::vec2 edge_a             = obb.edge_a;
                const glm::vec2 edge_b             = obb.edge_b;
                const glm::mat2 original_from_edge = glm::transpose(obb.edge_from_original);

                std::array<glm::vec3, 4> aabb_in_world{
                    unproject_nearplane_to_world(original_from_edge * glm::vec2{edge_aabb_min.x, edge_aabb_min.y}, 1.01f),
                    unproject_nearplane_to_world(original_from_edge * glm::vec2{edge_aabb_max.x, edge_aabb_min.y}, 1.01f),
                    unproject_nearplane_to_world(original_from_edge * glm::vec2{edge_aabb_max.x, edge_aabb_max.y}, 1.01f),
                    unproject_nearplane_to_world(original_from_edge * glm::vec2{edge_aabb_min.x, edge_aabb_max.y}, 1.01f)
                };
                const glm::vec4 pink{1.0f, 0.3f, 0.6f, 1.0f};
                for (std::size_t i = 0, count = aabb_in_world.size(); i < count; ++i) {
                    line_renderer.add_line(
                        pink, 1.0f, aabb_in_world[i              ],
                        pink, 1.0f, aabb_in_world[(i + 1) % count]
                    );
                }
                const glm::vec4 lime{0.4f, 1.0f, 0.1f, 1.0f};
                line_renderer.add_line(
                    lime, 3.0f, unproject_nearplane_to_world(edge_a, 1.02f),
                    lime, 3.0f, unproject_nearplane_to_world(edge_b, 1.02f)
                );
            }

        }
    }
}

void Debug_visualizations::physics_nodes_visualization(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    const auto& scene_root = context.scene_view.get_scene_root();
    const auto* camera     = context.camera;
    if (!scene_root || (camera == nullptr)) {
        return;
    }

    const auto projection_transforms = camera->projection_transforms(
        context.viewport,
        context.scene_view.get_reverse_depth(),
        context.scene_view.get_depth_range(),
        context.scene_view.get_conventions()
    );
    const glm::mat4 clip_from_world = projection_transforms.clip_from_world.get_matrix();

    for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
        for (const auto& mesh : layer->meshes) {
            if (!should_visualize(m_settings.physics, mesh)) {
                continue;
            }
            const auto* node = mesh->get_node();
            if (node == nullptr) {
                continue;
            }

            const auto& node_physics = erhe::scene::get_attachment<Node_physics>(node);
            if (!node_physics) {
                continue;
            }

            const erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
            if (rigid_body == nullptr) {
                continue;
            }
            const glm::mat4 m = rigid_body->get_world_transform();

            const glm::vec4 p4_in_world  = m * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
            const glm::vec3 p3_in_window = context.viewport.project_to_screen_space(
                clip_from_world,
                glm::vec3{p4_in_world},
                0.0f,
                1.0f,
                context.scene_view.get_conventions()
            );
            const auto label_text = "<" + node->describe() + ">"; // node_physics->describe();
            const glm::vec2 label_size = context.app_context.text_renderer->measure(label_text).size();
            const glm::vec3 p3_in_window_z_negated{
                 p3_in_window.x - label_size.x * 0.5,
                 p3_in_window.y - label_size.y * 0.5,
                -p3_in_window.z
            };
            glm::vec4 label_text_color{0.3f, 1.0f, 0.3f, 1.0f};
            const uint32_t text_color = erhe::math::convert_float4_to_uint32(label_text_color);

            context.app_context.text_renderer->print(
                p3_in_window_z_negated,
                text_color,
                label_text
            );
            const glm::vec3 dx{0.1f, 0.0f, 0.0f};
            const glm::vec3 dy{0.0f, 0.1f, 0.0f};
            const glm::vec3 dz{0.0f, 0.0f, 0.1f};
            for (const auto& marker : node_physics->markers) {
                const glm::vec4 blue{0.0f, 0.0f, 1.0f, 1.0f};
                line_renderer.add_lines(
                    m,
                    blue,
                    {
                        { marker - dx, marker + dx },
                        { marker - dy, marker + dy },
                        { marker - dz, marker + dz },
                    }
                );
            }

            {
                const glm::vec4 half_red  {0.5f, 0.0f, 0.0f, 0.5f};
                const glm::vec4 half_green{0.0f, 0.5f, 0.0f, 0.5f};
                const glm::vec4 half_blue {0.0f, 0.0f, 0.5f, 0.5f};
                line_renderer.add_lines( m, half_red,   {{ O, axis_x }} );
                line_renderer.add_lines( m, half_green, {{ O, axis_y }} );
                line_renderer.add_lines( m, half_blue,  {{ O, axis_z }} );
            }
            {
                const glm::vec4 cyan{0.0f, 1.0f, 1.0f, 0.5f};
                const glm::vec3 velocity = rigid_body->get_linear_velocity();
                line_renderer.add_lines( m, cyan, {{ O, 4.0f * velocity }} );
            }

            const auto collision_shape = rigid_body->get_collision_shape();
            if (!collision_shape) {
                continue;
            }
            {
                const glm::vec4 purple{1.0f, 0.0f, 1.0f, 1.0f};
                // This would return center of mass in world space
                //const glm::vec3 center_of_mass = rigid_body->get_center_of_mass();
                const glm::vec3 center_of_mass = collision_shape->get_center_of_mass();
                line_renderer.add_lines(
                    m,
                    purple,
                    {
                        { O, center_of_mass },
                        { center_of_mass - dx, center_of_mass + dx },
                        { center_of_mass - dy, center_of_mass + dy },
                        { center_of_mass - dz, center_of_mass + dz },
                    }
                );

            }
        }
    }

#if defined(ERHE_PHYSICS_LIBRARY_JOLT) && defined(JPH_DEBUG_RENDERER)
    App_context& app_context = context.app_context;
    if (app_context.editor_settings->physics.debug_draw) {
        glm::vec4 camera_position = camera->get_node()->position_in_world();
        const JPH::Vec3 camera_position_jolt{camera_position.x, camera_position.y, camera_position.z};
        app_context.jolt_debug_renderer->SetCameraPos(camera_position_jolt);
        erhe::physics::IWorld& world = scene_root->get_physics_world();
        world.debug_draw(*app_context.jolt_debug_renderer);
    }
#endif
}

void Debug_visualizations::raytrace_nodes_visualization(const Render_context& context)
{
    if (m_settings.raytrace == Visualization_mode::off) {
        return;
    }

    const auto& scene_root = context.scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }

    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    const glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    const glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};

    for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
        for (const auto& mesh : layer->meshes) {
            const auto* node = mesh->get_node();
            if (node == nullptr) {
                continue;
            }
            if (!should_visualize(m_settings.raytrace, node)) {
                continue;
            }

            for (const auto& rt_primitive : mesh->get_rt_primitives()) {
                const auto m = rt_primitive->rt_instance->get_transform();
                line_renderer.add_lines( m, red,   {{ O, axis_x }} );
                line_renderer.add_lines( m, green, {{ O, axis_y }} );
                line_renderer.add_lines( m, blue,  {{ O, axis_z }} );
            }
        }
    }
}

void Debug_visualizations::mesh_labels(const Render_context& context, erhe::scene::Mesh* scene_mesh)
{
    ERHE_PROFILE_FUNCTION();

    if (scene_mesh == nullptr) {
        return;
    }
    const erhe::scene::Node*   node   = scene_mesh->get_node();
    const erhe::scene::Camera* camera = context.camera;
    if ((node == nullptr) || (camera == nullptr)) {
        return;
    }
    const auto projection_transforms = camera->projection_transforms(
        context.viewport,
        context.scene_view.get_reverse_depth(),
        context.scene_view.get_depth_range(),
        context.scene_view.get_conventions()
    );
    const glm::mat4 clip_from_world = projection_transforms.clip_from_world.get_matrix();
    const glm::mat4 world_from_node = node->world_from_node();

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    erhe::scene::Mesh* hovered_scene_mesh{nullptr};
    const Hover_entry& content_hover = context.scene_view.get_hover(Hover_entry::content_slot);
    hovered_scene_mesh = content_hover.scene_mesh_weak.lock().get();

    for (erhe::scene::Mesh_primitive& mesh_primitive : scene_mesh->get_mutable_primitives()) {
        const erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
        if (!primitive.render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive.render_shape->get_geometry_const();
        if (!geometry) {
            continue;
        }

        const bool             is_mesh_hovered  = scene_mesh == hovered_scene_mesh;
        const bool             is_mesh_selected = scene_mesh->is_selected();
        const GEO::Mesh&       geo_mesh         = geometry->get_mesh();
        const Mesh_attributes& attributes       = geometry->get_attributes();

        if (should_visualize(m_settings.vertex_labels, is_mesh_selected, is_mesh_hovered)) {
            int label_count = 0;
            for (GEO::index_t vertex : geo_mesh.vertices) {
                const glm::vec3 p0 = to_glm_vec3(get_pointf(geo_mesh.vertices, vertex));
                glm::vec3 n{0.0f, 0.0f, 0.0f};
                const std::optional<GEO::vec3f> vertex_normal_smooth = attributes.vertex_normal_smooth.try_get(vertex);
                if (vertex_normal_smooth.has_value()) {
                    n = to_glm_vec3(vertex_normal_smooth.value());
                } else {
                    const std::optional<GEO::vec3f> vertex_normal = attributes.vertex_normal.try_get(vertex);
                    if (vertex_normal.has_value()) {
                        n = to_glm_vec3(vertex_normal.value());
                    }
                }
                const glm::vec3 p = p0 + m_settings.vertex_label_line_length * n;

                line_renderer.set_thickness(m_settings.vertex_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_settings.vertex_label_line_color,
                    { { p0, p } }
                );

                const std::string label_text = m_settings.vertex_positions ? fmt::format("{}: {}", vertex, p0) : fmt::format("{}", vertex);
                const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_settings.vertex_label_text_color);
                label(context, clip_from_world, world_from_node, p, text_color, label_text);
                if (++label_count >= m_settings.max_labels) {
                    break;
                }
            }
        }

        //auto polygon_normals = geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
        //if (polygon_normals == nullptr) {
        //    geometry->compute_polygon_normals();
        //    polygon_normals = geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
        //}

        if (should_visualize(m_settings.edge_labels, is_mesh_selected, is_mesh_hovered)) {
            int label_count = 0;
            const float t = m_settings.edge_label_line_length;
            for (GEO::index_t edge : geo_mesh.edges) {
                const GEO::index_t edge_a = geo_mesh.edges.vertex(edge, 0);
                const GEO::index_t edge_b = geo_mesh.edges.vertex(edge, 1);

                GEO::vec3f normal_sum{0.0f, 0.0f, 0.0f};

                const std::vector<GEO::index_t>& facets = geometry->get_edge_facets(edge);
                for (GEO::index_t facet : facets) {
                    GEO::vec3f facet_normal = GEO::normalize(mesh_facet_normalf(geo_mesh, facet));
                    normal_sum += facet_normal;
                }
                const GEO::vec3f n  = GEO::normalize(normal_sum);
                const GEO::vec3f a  = get_pointf(geo_mesh.vertices, edge_a) + 0.001f * n;
                const GEO::vec3f b  = get_pointf(geo_mesh.vertices, edge_b) + 0.001f * n;
                const GEO::vec3f p0 = (a + b) / 2.0f;
                const GEO::vec3f p  = p0 + m_settings.edge_label_text_offset * n;

                line_renderer.set_thickness(m_settings.edge_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_settings.edge_label_line_color,
                    {
                        {
                            to_glm_vec3(t * a + (1.0f - t) * p0),
                            to_glm_vec3(t * b + (1.0f - t) * p0)
                        },
                        {
                            to_glm_vec3(p0),
                            to_glm_vec3(p)
                        }
                    }
                );

                const std::string label_text = fmt::format("{}", edge);
                const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_settings.edge_label_text_color);
                label(context, clip_from_world, world_from_node, to_glm_vec3(p), text_color, label_text);
                if (++label_count >= m_settings.max_labels) {
                    break;
                }
            }
        }

        if (should_visualize(m_settings.facet_labels, is_mesh_selected, is_mesh_hovered)) {
            int label_count = 0;
            for (GEO::index_t facet : geo_mesh.facets) {
                if (!attributes.facet_centroid.has(facet)) {
                    continue;
                }
                const GEO::vec3f p = attributes.facet_centroid.get(facet);
                const GEO::vec3f n = GEO::normalize(mesh_facet_normalf(geo_mesh, facet));
                const GEO::vec3f l = p + m_settings.facet_label_line_length * n;

                line_renderer.set_thickness(m_settings.facet_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_settings.facet_label_line_color,
                    {{ to_glm_vec3(p), to_glm_vec3(l) }}
                );

                {
                    const std::string label_text = fmt::format("{}", facet);
                    const glm::vec4   p4_in_node = glm::vec4{to_glm_vec3(p) + m_settings.facet_label_line_length * to_glm_vec3(n), 1.0f};
                    const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_settings.facet_label_text_color);

                    label(context, clip_from_world, world_from_node, p4_in_node, text_color, label_text);
                }

                if (should_visualize(m_settings.corner_labels, is_mesh_selected, is_mesh_hovered)) {
                    for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                        const GEO::index_t vertex      = geo_mesh.facet_corners.vertex(corner);
                        const GEO::vec3f   corner_p    = get_pointf(geo_mesh.vertices, vertex);
                        const GEO::vec3f   to_centroid = GEO::normalize(l - corner_p);
                        const GEO::vec3f   label_p     = corner_p + m_settings.corner_label_line_length * to_centroid;

                        line_renderer.set_thickness(m_settings.corner_label_line_width);
                        line_renderer.add_lines(
                            world_from_node,
                            m_settings.corner_label_line_color,
                            {{ to_glm_vec3(corner_p), to_glm_vec3(label_p) }}
                        );

                        const std::string label_text = fmt::format("{}", corner);
                        const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_settings.corner_label_text_color);
                        label(context, clip_from_world, world_from_node, to_glm_vec3(label_p), text_color, label_text);
                        if (++label_count >= m_settings.max_labels) {
                            break;
                        }
                    }
                }
                if (++label_count >= m_settings.max_labels) {
                    break;
                }
            }
        }
    }
}

void Debug_visualizations::label(
    const Render_context&  context,
    const glm::mat4&       clip_from_world,
    const glm::mat4&       world_from_node,
    const glm::vec3&       position_in_world,
    const uint32_t         text_color,
    const std::string_view label_text
)
{
    const glm::vec4 p4_in_node   = glm::vec4{position_in_world, 1.0f};
    const glm::vec4 p4_in_world  = world_from_node * p4_in_node;
    const glm::vec3 p3_in_window = context.viewport.project_to_screen_space(
        clip_from_world,
        glm::vec3{p4_in_world},
        0.0f,
        1.0f,
        context.scene_view.get_conventions()
    );
    const glm::vec2 label_size = context.app_context.text_renderer->measure(label_text).size();
    const glm::vec3 p3_in_window_z_negated{
         p3_in_window.x - label_size.x * 0.5,
         p3_in_window.y - label_size.y * 0.5,
        -p3_in_window.z
    };
    context.app_context.text_renderer->print(p3_in_window_z_negated, text_color, label_text);
}

void Debug_visualizations::shadow_debug(const Render_context& render_context)
{
    const std::shared_ptr<Scene_root>& scene_root = render_context.scene_view.get_scene_root();
    const Scene_layers&                layers     = scene_root->layers();
    ERHE_VERIFY(render_context.render_pass != nullptr);
    // The Texel_renderer is a single shared instance owned by Editor; the
    // shadow_debug shader program is owned by Programs. Both are reached via
    // App_context (Debug_visualizations is per-view and default-constructed, so
    // it cannot own either).
    erhe::scene_renderer::Texel_renderer::Render_parameters parameters{
        .render_encoder    = *render_context.encoder,
        .shader_stages     = &render_context.app_context.programs->shadow_debug.shader_stages,
        .render_pass       = *render_context.render_pass,
        .camera            = render_context.camera,
        .light_projections = render_context.scene_view.get_light_projections(),
        .lights            = layers.light()->lights,
        .viewport          = render_context.viewport
    };
    render_context.app_context.texel_renderer->render(parameters);
}

void Debug_visualizations::world_axes_visualization(const Render_context& render_context)
{
    constexpr glm::vec3 o    {0.0f, 0.0f, 0.0f};
    constexpr glm::vec3 x    {1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 y    {0.0f, 1.0f, 0.0f};
    constexpr glm::vec3 z    {0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
    erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    line_renderer.set_thickness(-1.0f);
    line_renderer.add_lines(red,   {{o, 100.0f * x}});
    line_renderer.add_lines(green, {{o, 100.0f * y}});
    line_renderer.add_lines(blue,  {{o, 100.0f * z}});
}


void Debug_visualizations::render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    // render_viewport_renderables() runs twice per viewport: first the CPU
    // phase (no encoder) where debug lines / labels are generated, then the
    // encoder phase for renderables that issue draw calls. Everything below
    // generates lines / labels and must run only in the CPU phase.
    if (context.encoder != nullptr) {
        if (m_settings.shadow_debug) {
            shadow_debug(context);
        }
        return;
    }

    if (m_settings.world_axes) {
        world_axes_visualization(context);
    }

    if (
        m_settings.tool_hide &&
        context.app_context.transform_tool->is_transform_tool_active()
    ) {
        return;
    }

    std::shared_ptr<erhe::scene::Camera> selected_camera;
    const auto& selection = context.app_context.selection->get_selected_items();
    for (const auto& item : selection) {
        if (erhe::is<erhe::scene::Camera>(item)) {
            selected_camera = std::dynamic_pointer_cast<erhe::scene::Camera>(item);
        }
    }

    if (m_settings.selection) {
        selection_visualization(context);
    }

    const auto scene_root = context.scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    for (const auto& node : scene_root->get_hosted_scene()->get_flat_nodes()) {
        if (node && should_visualize(m_settings.node_axises, node)) {
            const glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
            const glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
            const glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
            const mat4 m{node->world_from_node()};
            line_renderer.set_thickness(m_selection_node_axis_width);
            line_renderer.add_lines( m, red,   {{ O, axis_x }} );
            line_renderer.add_lines( m, green, {{ O, axis_y }} );
            line_renderer.add_lines( m, blue,  {{ O, axis_z }} );
        }
    }

    for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
        for (const auto& mesh : layer->meshes) {
            mesh_labels(context, mesh.get());
        }
    }

    for (const auto& light : scene_root->layers().light()->lights) {
        if (should_visualize(m_settings.lights, light)) {
            light_visualization(context, selected_camera, light.get());
        }
    }

    if (m_settings.shadow_fit_debug) {
        shadow_frustum_fit_visualization(context);
    }

    //if (m_viewport_config->debug_visualizations.camera == Visualization_mode::all)
    for (const auto& camera : scene_root->get_scene().get_cameras()) {
        if (should_visualize(m_settings.cameras, camera)) {
            camera_visualization(context, camera.get());
        }
    }

    if (m_settings.layouts != Visualization_mode::off) {
        for (const auto& node : scene_root->get_hosted_scene()->get_flat_nodes()) {
            if (!node) {
                continue;
            }
            for (const auto& attachment : node->get_attachments()) {
                const auto& layout = std::dynamic_pointer_cast<erhe::scene::Layout>(attachment);
                if (layout && (should_visualize(m_settings.layouts, layout) || should_visualize(m_settings.layouts, node))) {
                    layout_visualization(context, *node, *layout);
                }
            }
        }
    }

    // Skins can be shared by multiple meshes.
    // Visualize each skin only once.
    if (m_settings.skins != Visualization_mode::off) {
        std::set<erhe::scene::Skin*> skins;
        for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
            for (const auto& mesh : layer->meshes) {
                if (mesh->skin && should_visualize(m_settings.skins, mesh)) {
                    skins.insert(mesh->skin.get());
                }
            }
        }
        for (auto* skin : skins) {
            skin_visualization(context, *skin);
        }
    }

    physics_nodes_visualization(context);

    raytrace_nodes_visualization(context);
}

void Debug_visualizations::make_combo(const char* label, Visualization_mode& visualization)
{
    erhe::imgui::make_combo(
        label,
        visualization,
        c_visualization_mode_strings,
        IM_ARRAYSIZE(c_visualization_mode_strings)
    );
}

void Debug_visualizations::imgui(Scene_view& scene_view, App_context& app_context)
{
    ERHE_PROFILE_FUNCTION();

    Property_editor& p = m_property_editor;
    const auto scene_view_camera = scene_view.get_camera();
    if (!scene_view_camera) {
        ImGui::Text("- Scene_view without Camera - ");
    } else {
        const std::string text = fmt::format(
            "- Scene_view with Camera {} @ {} - ",
            scene_view_camera->get_name(),
            glm::vec3{scene_view_camera->get_node()->position_in_world()}
        );
        ImGui::TextUnformatted(text.c_str());
    }

    p.reset();
    p.add_entry("Node Axises", [this](){ make_combo("##", m_settings.node_axises); });
    p.add_entry("Physics",     [this](){ make_combo("##", m_settings.physics  ); });
    p.add_entry("Raytrace",    [this](){ make_combo("##", m_settings.raytrace ); });

    p.add_entry("World Axes",   [this](){ ImGui::Checkbox   ("##", &m_settings.world_axes); });
    p.add_entry("Shadow Debug", [this](){ ImGui::Checkbox   ("##", &m_settings.shadow_debug); }); // shadow texel visualization
    p.add_entry("Shadow Fit",   [this](){ ImGui::Checkbox   ("##", &m_settings.shadow_fit_debug); });

    if (m_settings.shadow_fit_debug) {
        // Shadow frustum fit visualizations; data is collected per light only
        // when the Shadow Frustum Fit setting "Collect Debug Data" is enabled.
        // Each row: enable, color, line width (negative widths are in pixels
        // and do not scale by distance).
        auto shadow_fit_entry = [](bool* enable, glm::vec4* color, float* width) {
            if (enable != nullptr) {
                ImGui::Checkbox("##v", enable);
                ImGui::SameLine();
            }
            ImGui::ColorEdit4("##c", &color->x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
            if (width != nullptr) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("##w", width, 0.1f, -100.0f, 100.0f, "%.1f");
            }
        };
        const std::shared_ptr<Scene_root> shadow_fit_scene_root = scene_view.get_scene_root();
        p.push_group("Shadow Fit", ImGuiTreeNodeFlags_None);
        // Master switch for the fit's debug-data collection
        // (Shadow_frustum_fit_settings::collect_debug; the same setting as in
        // the Settings window, edited live). Every element below renders from
        // the collected data, so they are all empty while this is off. Keep it
        // off when profiling: with collection off the fit performs no
        // debug-related work at all.
        Editor_settings_config* const editor_settings = app_context.editor_settings;
        if (editor_settings != nullptr) {
            p.add_entry("Collect Debug Data", [editor_settings]() {
                ImGui::Checkbox("##", &editor_settings->shadow_frustum_fit.collect_debug);
            });
        }
        // Dump the current view's fit debug geometry once to the log (also
        // available over MCP as get_shadow_fit_debug). Needs Collect Debug on.
        p.add_entry("Dump", [this]() {
            if (ImGui::Button("Dump to Log")) {
                m_shadow_fit_dump_requested = true;
            }
        });
        // The fit and its caster classification target a single light.
        p.add_entry("Fit Light", [this, shadow_fit_scene_root]() {
            const std::shared_ptr<erhe::scene::Light> current = m_shadow_fit_light.lock();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::BeginCombo("##fit_light", current ? current->get_name().c_str() : "(first shadow light)")) {
                if (shadow_fit_scene_root) {
                    for (const std::shared_ptr<erhe::scene::Light>& light : shadow_fit_scene_root->layers().light()->lights) {
                        if (!light) {
                            continue;
                        }
                        const bool is_selected = (light == current);
                        if (ImGui::Selectable(light->get_name().c_str(), is_selected)) {
                            m_shadow_fit_light = light;
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
                ImGui::EndCombo();
            }
        });
        // Casters: enable, affecting color, culled color, line width.
        p.add_entry("Casters", [this]() {
            ImGui::Checkbox("##v", &m_settings.shadow_fit_casters);
            ImGui::SameLine();
            ImGui::ColorEdit4("##affecting", &m_settings.shadow_fit_casters_color.x,        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
            ImGui::SameLine();
            ImGui::ColorEdit4("##culled",    &m_settings.shadow_fit_casters_culled_color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("##w", &m_settings.shadow_fit_casters_width, 0.1f, -100.0f, 100.0f, "%.1f");
        });
        p.add_entry("Caster Hull",      [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_caster_hull,      &m_settings.shadow_fit_caster_hull_color,      &m_settings.shadow_fit_caster_hull_width); });
        p.add_entry("View Frustum",     [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_view_frustum,     &m_settings.shadow_fit_view_frustum_color,     &m_settings.shadow_fit_view_frustum_width); });
        // Receivers: enable, passing color, culled color, line width.
        p.add_entry("Receivers", [this]() {
            ImGui::Checkbox("##v", &m_settings.shadow_fit_receivers);
            ImGui::SameLine();
            ImGui::ColorEdit4("##pass",   &m_settings.shadow_fit_receivers_color.x,        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
            ImGui::SameLine();
            ImGui::ColorEdit4("##culled", &m_settings.shadow_fit_receivers_culled_color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("##w", &m_settings.shadow_fit_receivers_width, 0.1f, -100.0f, 100.0f, "%.1f");
        });
        p.add_entry("Receiver Hull Unclipped", [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_receiver_hull_unclipped, &m_settings.shadow_fit_receiver_hull_unclipped_color, &m_settings.shadow_fit_receiver_hull_unclipped_width); });
        p.add_entry("Receiver Hull Clipped",   [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_receiver_hull,           &m_settings.shadow_fit_receiver_hull_color,           &m_settings.shadow_fit_receiver_hull_width); });
        p.add_entry("Volume Planes",    [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_volume_planes,    &m_settings.shadow_fit_volume_planes_color,    &m_settings.shadow_fit_volume_planes_width); });
        p.add_entry("Far Plane Hull",   [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_far_plane_hull,   &m_settings.shadow_fit_far_plane_hull_color,   &m_settings.shadow_fit_far_plane_hull_width); });
        p.add_entry("Fit Points",       [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_points,           &m_settings.shadow_fit_points_color,           &m_settings.shadow_fit_points_width); });
        p.add_entry("Light Plane Hull", [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_light_plane_hull, &m_settings.shadow_fit_light_plane_hull_color, &m_settings.shadow_fit_light_plane_hull_width); });
        p.add_entry("OBB",              [this, shadow_fit_entry](){ shadow_fit_entry(nullptr,                        &m_settings.shadow_fit_obb_color,              &m_settings.shadow_fit_obb_width); });
        p.add_entry("OBB Edge",         [this, shadow_fit_entry](){ shadow_fit_entry(nullptr,                        &m_settings.shadow_fit_obb_edge_color,         &m_settings.shadow_fit_obb_edge_width); });
        p.add_entry("Box: Points",      [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_box_fit,          &m_settings.shadow_fit_box_fit_color,          nullptr); });
        p.add_entry("Box: Frustum",     [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_box_frustum,      &m_settings.shadow_fit_box_frustum_color,      nullptr); });
        p.add_entry("Box: Range Cap",   [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_box_cap,          &m_settings.shadow_fit_box_cap_color,          nullptr); });
        p.add_entry("Box: Final",       [this, shadow_fit_entry](){ shadow_fit_entry(&m_settings.shadow_fit_box_final,        &m_settings.shadow_fit_box_final_color,        nullptr); });
        p.add_entry("Box Width",        [this](){ ImGui::SetNextItemWidth(80.0f); ImGui::DragFloat("##", &m_settings.shadow_fit_box_width, 0.1f, -100.0f, 100.0f, "%.1f"); });
        p.pop_group();
    }

    p.push_group("Selection",  ImGuiTreeNodeFlags_None); //ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Selection",       [this](){ ImGui::Checkbox   ("##", &m_settings.selection); });
    p.add_entry("Bounding points", [this](){ ImGui::Checkbox   ("##", &m_settings.selection_bounding_points); });
    if (m_settings.selection_bounding_points) {
        erhe::math::Aabb   selection_bounding_box;
        erhe::math::Sphere selection_bounding_sphere;
        erhe::math::calculate_bounding_volume(m_selection_bounding_volume, selection_bounding_box, selection_bounding_sphere);
        const float box_volume    = selection_bounding_box.volume();
        const float sphere_volume = selection_bounding_sphere.volume();
        p.add_entry("Box Volume",    [this, box_volume               ](){ ImGui::Text("%f", box_volume); });
        p.add_entry("Sphere radius", [this, selection_bounding_sphere](){ ImGui::Text("%f", selection_bounding_sphere.radius); });
        p.add_entry("Sphere Volume", [this, sphere_volume            ](){ ImGui::Text("%f", sphere_volume); });
    }
    p.add_entry("Selection Box",            [this](){ ImGui::Checkbox   ("##", &m_settings.selection_box); });
    p.add_entry("Selection Sphere",         [this](){ ImGui::Checkbox   ("##", &m_settings.selection_sphere); });
    p.add_entry("Selection Parts",          [this](){ ImGui::Checkbox   ("##", &m_settings.selection_parts); });
    p.add_entry("Selection Convex Hull",    [this](){ ImGui::Checkbox   ("##", &m_settings.selection_convex_hull); });
    p.add_entry("Selection Projected Hull", [this](){ ImGui::Checkbox   ("##", &m_settings.selection_convex_hull_projected); });
    p.add_entry("Debug Convex Hull",        [this](){ ImGui::Checkbox   ("##", &m_settings.debug_convex_hull); });
    p.add_entry("Convex Hull Edge",         [this](){ ImGui::InputInt("##", &m_settings.convex_hull_edge, 1, 10, ImGuiInputTextFlags_None); });

    p.add_entry("Selection Major Color", [this](){ ImGui::ColorEdit4 ("##", &m_settings.selection_major_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Selection Minor Color", [this](){ ImGui::ColorEdit4 ("##", &m_settings.selection_minor_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Group Major Color",     [this](){ ImGui::ColorEdit4 ("##", &m_settings.group_selection_major_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Group Minor Color",     [this](){ ImGui::ColorEdit4 ("##", &m_settings.group_selection_minor_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Selection Major Width", [this](){ ImGui::SliderFloat("##", &m_settings.selection_major_width, 0.1f, 100.0f); });
    p.add_entry("Selection Minor Width", [this](){ ImGui::SliderFloat("##", &m_settings.selection_minor_width, 0.1f, 100.0f); });
    p.pop_group();

    p.add_entry("Sphere Step Count",     [this](){ ImGui::SliderInt  ("##", &m_settings.sphere_step_count, 1, 200); });
    p.add_entry("Gap",                   [this](){ ImGui::SliderFloat("##", &m_settings.gap, 0.0001f, 0.1f); });
    p.add_entry("Tool Hide",             [this](){ ImGui::Checkbox   ("##", &m_settings.tool_hide); });
    //ImGui::Checkbox   ("Raytrace",              &m_settings.raytrace);

    p.add_entry("Frustum Box",       [this](){ ImGui::Checkbox("##", &m_settings.frustum_box); });
    p.add_entry("Frustum Planes",    [this](){ ImGui::Checkbox("##", &m_settings.frustum_planes); });
    p.add_entry("Lights",            [this](){ make_combo("##", m_settings.lights); });
    if (m_settings.lights != Visualization_mode::off) {
        p.add_entry("Light Line Width", [this](){ ImGui::SliderFloat("##", &m_settings.light_line_width, 0.1f, 100.0f); });
    }
    p.add_entry("Cameras",           [this](){ make_combo("##", m_settings.cameras); });
    if (m_settings.cameras != Visualization_mode::off) {
        p.add_entry("Camera Cull Test",  [this](){ ImGui::Checkbox   ("##", &m_settings.camera_cull_test); });
        p.add_entry("Camera Line Width", [this](){ ImGui::SliderFloat("##", &m_settings.camera_line_width, 0.1f, 100.0f); });
        p.add_entry("Camera Line Color", [this](){ ImGui::ColorEdit4 ("##", &m_settings.camera_line_color.x, ImGuiColorEditFlags_Float); });
    }

    p.add_entry("Layouts",           [this](){ make_combo("##", m_settings.layouts); });
    if (m_settings.layouts != Visualization_mode::off) {
        p.add_entry("Layout Line Width", [this](){ ImGui::SliderFloat("##", &m_settings.layout_line_width, 0.1f, 100.0f); });
        p.add_entry("Layout Line Color", [this](){ ImGui::ColorEdit4 ("##", &m_settings.layout_line_color.x, ImGuiColorEditFlags_Float); });
    }

    p.add_entry("Skins",          [this](){ make_combo("##", m_settings.skins); });

    p.push_group("Annotiations", ImGuiTreeNodeFlags_None); //ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Max Labels", [this](){ ImGui::SliderInt("##", &m_settings.max_labels, 0, 2000); });
    
    p.add_entry("Show Vertices",    [this](){ make_combo("##", m_settings.vertex_labels); });
    p.add_entry("Vertex Positions", [this](){ ImGui::Checkbox("##", &m_settings.vertex_positions); });
    p.add_entry("Show Facets",      [this](){ make_combo("##", m_settings.facet_labels ); });
    p.add_entry("Show Edges",       [this](){ make_combo("##", m_settings.edge_labels  ); });
    p.add_entry("Show Corners",     [this](){ make_combo("##", m_settings.corner_labels); });

    p.push_group("Style", ImGuiTreeNodeFlags_None); //ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Vertex Label Text Color",  [this](){ ImGui::ColorEdit4 ("##", &m_settings.vertex_label_text_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Vertex Label Line Color",  [this](){ ImGui::ColorEdit4 ("##", &m_settings.vertex_label_line_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Vertex Label Line Width",  [this](){ ImGui::SliderFloat("##", &m_settings.vertex_label_line_width,   0.0f, 4.0f); });
    p.add_entry("Vertex Label Line Length", [this](){ ImGui::SliderFloat("##", &m_settings.vertex_label_line_length,  0.0f, 1.0f); });
    p.add_entry("Edge Label Text Color",    [this](){ ImGui::ColorEdit4 ("##", &m_settings.edge_label_text_color.x,   ImGuiColorEditFlags_Float); });
    p.add_entry("Edge Label Text Offset",   [this](){ ImGui::SliderFloat("##", &m_settings.edge_label_text_offset,    0.0f, 1.0f); });
    p.add_entry("Edge Label Line Color",    [this](){ ImGui::ColorEdit4 ("##", &m_settings.edge_label_line_color.x,   ImGuiColorEditFlags_Float); });
    p.add_entry("Edge Label Line Width",    [this](){ ImGui::SliderFloat("##", &m_settings.edge_label_line_width,     0.0f, 4.0f); });
    p.add_entry("Edge Label Line Length",   [this](){ ImGui::SliderFloat("##", &m_settings.edge_label_line_length,    0.0f, 1.0f); });
    p.add_entry("Facet Label Text Color",   [this](){ ImGui::ColorEdit4 ("##", &m_settings.facet_label_text_color.x,  ImGuiColorEditFlags_Float); });
    p.add_entry("Facet Label Line Color",   [this](){ ImGui::ColorEdit4 ("##", &m_settings.facet_label_line_color.x,  ImGuiColorEditFlags_Float); });
    p.add_entry("Facet Label Line Width",   [this](){ ImGui::SliderFloat("##", &m_settings.facet_label_line_width,    0.0f, 4.0f); });
    p.add_entry("Facet Label Line Length",  [this](){ ImGui::SliderFloat("##", &m_settings.facet_label_line_length,   0.0f, 1.0f); });
    p.add_entry("Corner Label Text Color",  [this](){ ImGui::ColorEdit4 ("##", &m_settings.corner_label_text_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Corner Label Line Color",  [this](){ ImGui::ColorEdit4 ("##", &m_settings.corner_label_line_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Corner Label Line Width",  [this](){ ImGui::SliderFloat("##", &m_settings.corner_label_line_width,   0.0f, 4.0f); });
    p.add_entry("Corner Label Line Length", [this](){ ImGui::SliderFloat("##", &m_settings.corner_label_line_length,  0.0f, 1.0f); });
    p.pop_group();
    p.pop_group();
    p.show_entries();
}

}

