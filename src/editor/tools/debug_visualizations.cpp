#include "tools/debug_visualizations.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "renderers/render_context.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "tools/selection_tool.hpp"
#include "tools/transform/transform_tool.hpp"

#include "erhe/renderer/line_renderer.hpp"
#include "erhe/renderer/text_renderer.hpp"
#include "time.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/scene_renderer/shadow_renderer.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/skin.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::mat4;
using glm::vec3;
using glm::vec4;
using Trs_transform = erhe::scene::Trs_transform;

namespace
{

[[nodiscard]] auto sign(const float x) -> float { return x < 0.0f ? -1.0f : 1.0f; }

constexpr vec3 clip_min_corner{-1.0f, -1.0f, 0.0f};
constexpr vec3 clip_max_corner{ 1.0f,  1.0f, 1.0f};
constexpr vec3 O              { 0.0f };
constexpr vec3 axis_x         { 1.0f,  0.0f, 0.0f};
constexpr vec3 axis_y         { 0.0f,  1.0f, 0.0f};
constexpr vec3 axis_z         { 0.0f,  0.0f, 1.0f};

}


Debug_visualizations::Debug_visualizations(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus,
    Editor_rendering&            editor_rendering
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Debug Visualizations", "debug_visualizations"}
    , m_context{editor_context}
{
    editor_rendering.add(this);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            using namespace erhe::toolkit;
            if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
                m_hover_scene_view = message.scene_view;
            }
        }
    );
}

auto Debug_visualizations::get_selected_camera(
    const Render_context& render_context
) -> std::shared_ptr<erhe::scene::Camera>
{
    const auto* scene     = render_context.get_scene();
    const auto& selection = m_context.selection->get_selection();

    for (const auto& scene_item : selection) {
        const auto& node = as_node(scene_item);
        if (node) {
            if (node->get_scene() != scene) {
                continue;
            }
            for (const auto& attachment : node->attachments()) {
                const auto camera = as_camera(attachment);
                if (camera) {
                    return camera;
                }
            }
        }
    }

    return {};
}

void Debug_visualizations::mesh_selection_visualization(
    const Render_context& render_context,
    erhe::scene::Mesh*    mesh
)
{
    if (mesh == nullptr) {
        return;
    }
    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

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

    for (const auto& primitive : mesh->mesh_data.primitives) {
        if (!primitive.source_geometry) {
            continue;
        }
        const auto& primitive_geometry = primitive.gl_primitive_geometry;

        const float box_volume    = primitive_geometry.bounding_box.volume();
        const float sphere_volume = primitive_geometry.bounding_sphere.volume();
        //const bool  smallest_visualization = !m_viewport_config->selection_bounding_box && !m_viewport_config->selection_bounding_sphere;
        const bool  box_smaller   = box_volume < sphere_volume;

        if (box_smaller) {
            m_selection_bounding_volume.add_box(
                node->world_from_node(),
                primitive_geometry.bounding_box.min,
                primitive_geometry.bounding_box.max
            );
        }
        if (
            m_selection_box ||
            (
                box_smaller &&
                !m_selection_sphere
            )
        ) {
            line_renderer.set_thickness(m_selection_major_width);
            line_renderer.add_cube(
                node->world_from_node(),
                m_selection_major_color,
                primitive_geometry.bounding_box.min - glm::vec3{m_gap, m_gap, m_gap},
                primitive_geometry.bounding_box.max + glm::vec3{m_gap, m_gap, m_gap}
            );
        }
        if (!box_smaller) {
            m_selection_bounding_volume.add_sphere(
                node->world_from_node(),
                primitive_geometry.bounding_sphere.center,
                primitive_geometry.bounding_sphere.radius
            );
        }
        if (
            m_selection_sphere ||
            (
                !box_smaller &&
                !m_selection_box
            )
        ) {
            if (used_camera) {
                line_renderer.add_sphere(
                    node->world_from_node_transform(),
                    m_selection_major_color,
                    m_selection_minor_color,
                    m_selection_major_width,
                    m_selection_minor_width,
                    primitive_geometry.bounding_sphere.center,
                    primitive_geometry.bounding_sphere.radius + m_gap,
                    &camera_world_from_node_transform,
                    m_sphere_step_count
                );
            }
        }
    }

    if (m_show_only_selection) {
        mesh_labels(render_context, mesh);
    }
}

void Debug_visualizations::skin_visualization(
    const Render_context& render_context,
    erhe::scene::Skin*    skin
)
{
    if (skin == nullptr) {
        return;
    }
    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    const auto* node = skin->get_node();
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

    //constexpr vec4 red  { 1.0f, 0.0f, 0.0f, 1.0f};
    //constexpr vec4 green{ 0.0f, 1.0f, 0.0f, 1.0f};
    //constexpr vec4 blue { 0.0f, 0.0f, 1.0f, 1.0f};
    //constexpr vec4 cyan { 0.0f, 1.0f, 1.0f, 1.0f};
    line_renderer.set_thickness(2.0f);

    for (std::size_t i = 0, end_i = skin->skin_data.joints.size(); i < end_i; ++i) {
        const auto& joint = skin->skin_data.joints[i];
        if (!joint) {
            continue;
        }
        const mat4 world_from_joint = joint->world_from_node();

        line_renderer.set_line_color(joint->get_wireframe_color());
        vec3 a = joint->position_in_world();
        vec3 b = a + vec3{0.2f, 0.0f, 0.0f};

        // Search for child to connect bone tip:
        bool child_found = false;
        for (std::size_t j = 0, end_j = skin->skin_data.joints.size(); j < end_j; ++j) {
            if (j == i) {
                continue;
            }
            const auto& other_joint = skin->skin_data.joints[j];
            if (other_joint->parent().lock() == joint) {
                b = other_joint->position_in_world();
                child_found = true;
                break;
            }
        }
        if (!child_found) {
            // No child, try to guess bone tip compared to parent (if it has parent):
            const auto& parent = joint->parent().lock();
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
    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(light->get_flag_bits(), erhe::scene::Item_flags::show_debug_visualizations)) {
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

void Debug_visualizations::directional_light_visualization(
    const Light_visualization_context& context
)
{
    const auto shadow_render_node = m_context.editor_rendering->get_shadow_node_for_view(context.render_context.scene_view);
    if (!shadow_render_node) {
        return;
    }

    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();
    const auto& light_projections           = shadow_render_node->get_light_projections();
    const auto* light                       = context.light;
    const auto  light_projection_transforms = light_projections.get_light_projection_transforms_for_light(light);

    if (light_projection_transforms == nullptr) {
        return;
    }

    const glm::mat4 world_from_light_clip   = light_projection_transforms->clip_from_world.get_inverse_matrix();
    const glm::mat4 world_from_light_camera = light_projection_transforms->world_from_light_camera.get_matrix();

    line_renderer.set_thickness(m_light_visualization_width);
    line_renderer.add_cube(
        world_from_light_clip,
        context.light_color,
        clip_min_corner,
        clip_max_corner
    );

    line_renderer.add_lines(
        world_from_light_camera,
        context.light_color,
        {{ O, -1.0f * axis_z }}
    );
}

void Debug_visualizations::point_light_visualization(
    const Light_visualization_context& context
)
{
    const auto* node = context.light->get_node();
    if (node == nullptr) {
        return;
    }
    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    constexpr float scale = 0.5f;
    const auto nnn = scale * glm::normalize(-axis_x - axis_y - axis_z);
    const auto nnp = scale * glm::normalize(-axis_x - axis_y + axis_z);
    const auto npn = scale * glm::normalize(-axis_x + axis_y - axis_z);
    const auto npp = scale * glm::normalize(-axis_x + axis_y + axis_z);
    const auto pnn = scale * glm::normalize( axis_x - axis_y - axis_z);
    const auto pnp = scale * glm::normalize( axis_x - axis_y + axis_z);
    const auto ppn = scale * glm::normalize( axis_x + axis_y - axis_z);
    const auto ppp = scale * glm::normalize( axis_x + axis_y + axis_z);
    line_renderer.set_thickness(m_light_visualization_width);
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

void Debug_visualizations::spot_light_visualization(
    const Light_visualization_context& context
)
{
    const auto* node = context.light->get_node();
    if (node == nullptr) {
        return;
    }

    const auto* camera_node = context.render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();
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

    //auto* editor_time = get<Editor_time>();
    //const float time = static_cast<float>(editor_time->time());
    //const float half_position = (editor_time != nullptr)
    //    ? time - floor(time)
    //    : 0.5f;

    line_renderer.set_thickness(m_light_visualization_width);

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

        cone_edges.emplace_back(
            p,
            N,
            T,
            B,
            phi,
            n_dot_v
        );
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

void Debug_visualizations::camera_visualization(
    const Render_context&       render_context,
    const erhe::scene::Camera*  camera
)
{
    if (camera == &render_context.camera) {
        return;
    }

    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(camera->get_flag_bits(), erhe::scene::Item_flags::show_debug_visualizations)) {
        return;
    }

    const auto* node = camera->get_node();
    if (node == nullptr) {
        return;
    }

    //const auto* view_camera_node = render_context.get_camera_node();
    //if (view_camera_node == nullptr)
    //{
    //    return;
    //}

    const mat4 clip_from_node  = camera->projection()->get_projection_matrix(1.0f, render_context.viewport.reverse_depth);
    const mat4 node_from_clip  = inverse(clip_from_node);
    const mat4 world_from_clip = node->world_from_node() * node_from_clip;

    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();
    line_renderer.set_thickness(m_camera_visualization_width);
    line_renderer.add_cube(
        world_from_clip,
        camera->get_wireframe_color(),
        clip_min_corner,
        clip_max_corner,
        true
    );
}

void Debug_visualizations::selection_visualization(
    const Render_context& context
)
{
    const auto* scene = context.get_scene();
    if (scene == nullptr) {
        return;
    }

    const auto* viewport_config = context.viewport_window->get_config();

    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    const auto& selection = m_context.selection->get_selection();

    m_selection_bounding_volume = erhe::toolkit::Bounding_volume_combiner{}; // reset
    for (const auto& scene_item : selection) {
        const auto& node = as_node(scene_item);
        if (node) {
            if (node->get_scene() != scene) {
                continue;
            }
            if (m_selection_node_axis_visible) {
                const glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
                const glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
                const glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
                const mat4 m{node->world_from_node()};
                line_renderer.set_thickness(m_selection_node_axis_width);
                line_renderer.add_lines( m, red,   {{ O, axis_x }} );
                line_renderer.add_lines( m, green, {{ O, axis_y }} );
                line_renderer.add_lines( m, blue,  {{ O, axis_z }} );
            }
            for (const auto& attachment : node->attachments()) {
                const auto mesh = as_mesh(attachment);
                if (mesh) {
                    mesh_selection_visualization(context, mesh.get());
                }
                //const auto skin = as_skin(attachment);
                //if (skin) {
                //    skin_visualization(context, skin.get());
                //}

                const auto camera = as_camera(attachment);
                if (
                    camera &&
                    (viewport_config != nullptr) &&
                    (viewport_config->debug_visualizations.camera == erhe::renderer::Visualization_mode::selected)
                ) {
                    camera_visualization(context, camera.get());
                }
            }
        }
    }

    if (m_selection_bounding_volume.get_element_count() > 1) {
        if (m_selection_bounding_points_visible) {
            const auto&     camera                = context.camera;
            const auto      projection_transforms = camera.projection_transforms(context.viewport);
            const glm::mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();

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
                        1.0f
                    );
                    const uint32_t  text_color = 0xff88ff88u;
                    const glm::vec3 point_in_window_z_negated{
                         point_in_window.x,
                         point_in_window.y,
                        -point_in_window.z
                    };
                    m_context.text_renderer->print(
                        point_in_window_z_negated,
                        text_color,
                        fmt::format("{}.{}", i, j)
                    );
                }
            }
        }

        erhe::toolkit::Bounding_box    selection_bounding_box;
        erhe::toolkit::Bounding_sphere selection_bounding_sphere;
        erhe::toolkit::calculate_bounding_volume(m_selection_bounding_volume, selection_bounding_box, selection_bounding_sphere);
        const float box_volume    = selection_bounding_box.volume();
        const float sphere_volume = selection_bounding_sphere.volume();
        if (
            m_selection_box ||
            (
                !m_selection_sphere &&
                (box_volume > 0.0f) &&
                (box_volume < sphere_volume)
            )
        ) {
            line_renderer.set_thickness(m_selection_major_width);
            line_renderer.add_cube(
                glm::mat4{1.0f},
                m_group_selection_major_color,
                selection_bounding_box.min - glm::vec3{m_gap, m_gap, m_gap},
                selection_bounding_box.max + glm::vec3{m_gap, m_gap, m_gap}
            );
        }
        if (
            m_selection_sphere ||
            (
                !m_selection_box &&
                (sphere_volume > 0.0f) &&
                (sphere_volume < box_volume)
            )
        ) {
            const auto* camera_node = context.get_camera_node();
            if (camera_node != nullptr) {
                line_renderer.add_sphere(
                    erhe::scene::Transform{},
                    m_group_selection_major_color,
                    m_group_selection_minor_color,
                    m_selection_major_width,
                    m_selection_minor_width,
                    selection_bounding_sphere.center,
                    selection_bounding_sphere.radius + m_gap,
                    &(camera_node->world_from_node_transform()),
                    m_sphere_step_count
                );
            }
        }
    }
}

void Debug_visualizations::physics_nodes_visualization(
    const std::shared_ptr<Scene_root>& scene_root
)
{
    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    for (const auto& mesh : scene_root->layers().content()->meshes) {
        const auto* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }

        const auto& node_physics = get_node_physics(node);
        if (node_physics) {
            const erhe::physics::IRigid_body* rigid_body = node_physics->rigid_body();
            if (rigid_body != nullptr) {
                const erhe::physics::Transform transform = rigid_body->get_world_transform();
                {
                    const glm::vec4 half_red  {0.5f, 0.0f, 0.0f, 0.5f};
                    const glm::vec4 half_green{0.0f, 0.5f, 0.0f, 0.5f};
                    const glm::vec4 half_blue {0.0f, 0.0f, 0.5f, 0.5f};
                    glm::mat4 m{transform.basis};
                    m[3] = glm::vec4{
                        transform.origin.x,
                        transform.origin.y,
                        transform.origin.z,
                        1.0f
                    };
                    line_renderer.add_lines( m, half_red,   {{ O, axis_x }} );
                    line_renderer.add_lines( m, half_green, {{ O, axis_y }} );
                    line_renderer.add_lines( m, half_blue,  {{ O, axis_z }} );
                }
                {
                    const glm::vec4 cyan{0.0f, 1.0f, 1.0f, 1.0f};
                    const glm::vec3 velocity = rigid_body->get_linear_velocity();
                    glm::mat4 m{1.0f};
                    m[3] = glm::vec4{
                        transform.origin.x,
                        transform.origin.y,
                        transform.origin.z,
                        1.0f
                    };
                    line_renderer.add_lines( m, cyan, {{ O, 4.0f * velocity }} );
                }
            }
        }
    }
}

void Debug_visualizations::raytrace_nodes_visualization(
    const std::shared_ptr<Scene_root>& scene_root
)
{
    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    const glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    const glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};

    for (const auto& mesh : scene_root->layers().content()->meshes) {
        const auto* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }

        const auto& node_raytrace = get_raytrace(node);
        if (node_raytrace) {
            const erhe::raytrace::IInstance* instance = node_raytrace->raytrace_instance();
            const auto m = instance->get_transform();
            line_renderer.add_lines( m, red,   {{ O, axis_x }} );
            line_renderer.add_lines( m, green, {{ O, axis_y }} );
            line_renderer.add_lines( m, blue,  {{ O, axis_z }} );
        }
    }
}

void Debug_visualizations::mesh_labels(
    const Render_context& context,
    erhe::scene::Mesh*    mesh
)
{
    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    if (mesh == nullptr) {
        return;
    }
    const auto* node = mesh->get_node();
    if (node == nullptr) {
        return;
    }
    const auto&     camera                = context.camera;
    const auto      projection_transforms = camera.projection_transforms(context.viewport);
    const glm::mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();

    const glm::mat4 world_from_node = node->world_from_node();
    for (auto& primitive : mesh->mesh_data.primitives) {
        const auto& geometry = primitive.source_geometry;
        if (!geometry) {
            continue;
        }

        const auto* point_locations = geometry->point_attributes().find<glm::vec3>(erhe::geometry::c_point_locations);
        if ((point_locations != nullptr) && m_show_points) {
            auto* point_normals        = geometry->point_attributes().find<glm::vec3>(erhe::geometry::c_point_normals);
            auto* point_normals_smooth = geometry->point_attributes().find<glm::vec3>(erhe::geometry::c_point_normals_smooth);

            const uint32_t end = (std::min)(
                static_cast<uint32_t>(m_max_labels),
                geometry->get_point_count()
            );
            for (erhe::geometry::Point_id point_id = 0; point_id < end; ++point_id) {
                if (!point_locations->has(point_id)) {
                    continue;
                }

                const auto p0 = point_locations->get(point_id);
                glm::vec3  n{0.0f, 0.0f, 0.0f};
                if ((point_normals_smooth == nullptr) || !point_normals_smooth->maybe_get(point_id, n)) {
                    if (point_normals != nullptr) {
                        point_normals->maybe_get(point_id, n);
                    }
                }
                const auto p  = p0 + m_point_label_line_length * n;

                line_renderer.set_thickness(m_point_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_point_label_line_color,
                    {
                        {
                            p0,
                            p
                        }
                    }
                );

                const std::string label_text = fmt::format("{}", point_id);
                const uint32_t    text_color = erhe::toolkit::convert_float4_to_uint32(m_point_label_text_color);
                label(context, clip_from_world, world_from_node, p, text_color, label_text);
            }
        }

        auto polygon_normals = geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals  );
        if (polygon_normals == nullptr) {
            geometry->compute_polygon_normals();
            polygon_normals = geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals  );
        }

        if ((point_locations != nullptr) && m_show_edges) {
            const uint32_t end = (std::min)(
                static_cast<uint32_t>(m_max_labels),
                geometry->get_edge_count()
            );

            const float t = m_edge_label_line_length;
            for (erhe::geometry::Edge_id edge_id = 0; edge_id < end; ++edge_id) {
                const auto& edge = geometry->edges[edge_id];

                if (!point_locations->has(edge.a) || !point_locations->has(edge.b)) {
                    continue;
                }

                glm::vec3   normal_sum{0.0f, 0.0f, 0.0f};
                std::size_t polygon_count{0};
                edge.for_each_polygon_const(
                    *geometry.get(),
                    [&normal_sum, &polygon_normals, &polygon_count, this, edge_id]
                    (erhe::geometry::Edge::Edge_polygon_context_const& context)
                    {
                        glm::vec3 polygon_normal;
                        if (polygon_normals->maybe_get(context.polygon_id, polygon_normal)) {
                            normal_sum += polygon_normal;
                            ++polygon_count;
                        }
                    }
                );
                const auto n  = glm::normalize(normal_sum);
                const auto a  = point_locations->get(edge.a) + 0.001f * n;
                const auto b  = point_locations->get(edge.b) + 0.001f * n;
                const auto p0 = (a + b) / 2.0f;
                const auto p  = p0 + m_edge_label_text_offset * n;

                line_renderer.set_thickness(m_edge_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_edge_label_line_color,
                    {
                        {
                            t * a + (1.0f - t) * p0,
                            t * b + (1.0f - t) * p0
                        },
                        {
                            p0,
                            p
                        }
                    }
                );

                const std::string label_text = fmt::format("{}", edge_id);
                const uint32_t    text_color = erhe::toolkit::convert_float4_to_uint32(m_edge_label_text_color);
                label(context, clip_from_world, world_from_node, p, text_color, label_text);
            }
        }

        const auto polygon_centroids = geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_centroids);
        if ((polygon_centroids != nullptr) && m_show_polygons) {
            const uint32_t end = (std::min)(
                static_cast<uint32_t>(m_max_labels),
                geometry->get_polygon_count()
            );
            for (erhe::geometry::Polygon_id polygon_id = 0; polygon_id < end; ++polygon_id) {
                if (!polygon_centroids->has(polygon_id)) {
                    continue;
                }
                if (!polygon_normals->has(polygon_id)) {
                    continue;
                }
                const glm::vec3 p = polygon_centroids->get(polygon_id);
                const glm::vec3 n = polygon_normals  ->get(polygon_id);
                const glm::vec3 l = p + m_polygon_label_line_length * n;

                line_renderer.set_thickness(m_polygon_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_polygon_label_line_color,
                    { { p, l } }
                );

                const std::string label_text = fmt::format("{}", polygon_id);
                const glm::vec4   p4_in_node = glm::vec4{p + m_polygon_label_line_length * n, 1.0f};
                const uint32_t    text_color = erhe::toolkit::convert_float4_to_uint32(m_polygon_label_text_color);

                label(context, clip_from_world, world_from_node, p4_in_node, text_color, label_text);

                if (m_show_corners) {
                    const erhe::geometry::Polygon polygon = geometry->polygons.at(polygon_id);
                    polygon.for_each_corner_const(*geometry.get(), [&](auto& i)
                    {
                        const auto corner_p    = point_locations->get(i.corner.point_id);
                        const auto to_centroid = glm::normalize(l - corner_p);
                        const auto label_p     = corner_p + m_corner_label_line_length * to_centroid;

                        line_renderer.set_thickness(m_corner_label_line_width);
                        line_renderer.add_lines(
                            world_from_node,
                            m_corner_label_line_color,
                            { { corner_p, label_p } }
                        );

                        const std::string label_text = fmt::format("{}", i.corner_id);
                        const uint32_t    text_color = erhe::toolkit::convert_float4_to_uint32(m_corner_label_text_color);
                        label(context, clip_from_world, world_from_node, label_p, text_color, label_text);
                    });
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
        1.0f
    );
    const glm::vec2 label_size = m_context.text_renderer->measure(label_text).size();
    const glm::vec3 p3_in_window_z_negated{
         p3_in_window.x - label_size.x * 0.5,
         p3_in_window.y - label_size.x * 0.5,
        -p3_in_window.z
    };
    m_context.text_renderer->print(
        p3_in_window_z_negated,
        text_color,
        label_text
    );
}


void Debug_visualizations::render(
    const Render_context& context
)
{
    if (
        m_tool_hide &&
        m_context.transform_tool->is_transform_tool_active()
    ) {
        return;
    }

    std::shared_ptr<erhe::scene::Camera> selected_camera;
    const auto& selection = m_context.selection->get_selection();
    for (const auto& node : selection) {
        if (is_camera(node)) {
            selected_camera = as_camera(node);
        }
    }

    if (m_selection) {
        selection_visualization(context);
    }

    const auto scene_root = context.scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }

    if (!m_show_only_selection) {
        for (const auto& mesh : scene_root->layers().content()->meshes) {
            mesh_labels(context, mesh.get());
        }
    }

    if (m_lights) {
        for (const auto& light : scene_root->layers().light()->lights) {
            light_visualization(context, selected_camera, light.get());
        }
    }

    //if (m_viewport_config->debug_visualizations.camera == Visualization_mode::all)
    if (m_cameras) {
        for (const auto& camera : scene_root->scene().get_cameras()) {
            camera_visualization(context, camera.get());
        }
    }

    if (m_skins) {
        for (const auto& skin : scene_root->scene().get_skins()) {
            skin_visualization(context, skin.get());
        }
    }

    if (m_physics) {
        physics_nodes_visualization(scene_root);
    }

    if (m_raytrace) {
        raytrace_nodes_visualization(scene_root);
    }
}

void Debug_visualizations::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    //// for (const auto& line : m_lines)
    //// {
    ////     ImGui::TextUnformatted(line.c_str());
    //// }
    //// m_lines.clear();
    if (m_hover_scene_view == nullptr) {
        ImGui::Text("- No Scene_view - ");
    } else {
        const auto scene_view_camera = m_hover_scene_view->get_camera();
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
    }

    ImGui::Checkbox   ("Selection Axises",      &m_selection_node_axis_visible);
    ImGui::Checkbox   ("Selection Box",         &m_selection_box);
    ImGui::Checkbox   ("Selection Sphere",      &m_selection_sphere);
    ImGui::ColorEdit4 ("Selection Major Color", &m_selection_major_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Selection Minor Color", &m_selection_minor_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Group Major Color",     &m_group_selection_major_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Group Minor Color",     &m_group_selection_minor_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Selection Major Width", &m_selection_major_width, 0.1f, 100.0f);
    ImGui::SliderFloat("Selection Minor Width", &m_selection_minor_width, 0.1f, 100.0f);
    ImGui::SliderInt  ("Sphere Step Count",     &m_sphere_step_count, 1, 200);
    ImGui::SliderFloat("Gap",                   &m_gap, 0.0001f, 0.1f);
    ImGui::Checkbox   ("Tool Hide",             &m_tool_hide);
    ImGui::Checkbox   ("Raytrace",              &m_raytrace);
    ImGui::Checkbox   ("Physics",               &m_physics);
    ImGui::Checkbox   ("Lights",                &m_lights);
    ImGui::Checkbox   ("Cameras",               &m_cameras);
    ImGui::Checkbox   ("Skins",                 &m_skins);
    ImGui::Checkbox   ("Selection",             &m_selection);
    ImGui::Checkbox   ("Bounding points",       &m_selection_bounding_points_visible);
    if (m_selection_bounding_points_visible) {
        erhe::toolkit::Bounding_box    selection_bounding_box;
        erhe::toolkit::Bounding_sphere selection_bounding_sphere;
        erhe::toolkit::calculate_bounding_volume(m_selection_bounding_volume, selection_bounding_box, selection_bounding_sphere);
        const float    box_volume    = selection_bounding_box.volume();
        const float    sphere_volume = selection_bounding_sphere.volume();
        ImGui::Text("Box Volume: %f", box_volume);
        ImGui::Text("Sphere radius: %f", selection_bounding_sphere.radius);
        ImGui::Text("Sphere Volume: %f", sphere_volume);
    }
    ImGui::SliderInt("Max Labels",     &m_max_labels, 0, 2000);
    ImGui::Checkbox ("Only Selection", &m_show_only_selection);
    ImGui::Checkbox ("Show Points",    &m_show_points);
    ImGui::Checkbox ("Show Polygons",  &m_show_polygons);
    ImGui::Checkbox ("Show Edges",     &m_show_edges);
    ImGui::Checkbox ("Show Corners",   &m_show_corners);
    ImGui::ColorEdit4 ("Point Label Text Color",    &m_point_label_text_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Point Label Line Color",    &m_point_label_line_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Point Label Line Width",    &m_point_label_line_width,   0.0f, 4.0f);
    ImGui::SliderFloat("Point Label Line Length",   &m_point_label_line_length,  0.0f, 1.0f);
    ImGui::ColorEdit4 ("Edge Label Text Color",     &m_edge_label_text_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Edge Label Text Offset",    &m_edge_label_text_offset,  0.0f, 1.0f);
    ImGui::ColorEdit4 ("Edge Label Line Color",     &m_edge_label_line_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Edge Label Line Width",     &m_edge_label_line_width,   0.0f, 4.0f);
    ImGui::SliderFloat("Edge Label Line Length",    &m_edge_label_line_length,  0.0f, 1.0f);
    ImGui::ColorEdit4 ("Polygon Label Text Color",  &m_polygon_label_text_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Polygon Label Line Color",  &m_polygon_label_line_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Polygon Label Line Width",  &m_polygon_label_line_width,   0.0f, 4.0f);
    ImGui::SliderFloat("Polygon Label Line Length", &m_polygon_label_line_length,  0.0f, 1.0f);
    ImGui::ColorEdit4 ("Corner Label Text Color",  &m_corner_label_text_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Corner Label Line Color",  &m_corner_label_line_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Corner Label Line Width",  &m_corner_label_line_width,   0.0f, 4.0f);
    ImGui::SliderFloat("Corner Label Line Length", &m_corner_label_line_length,  0.0f, 1.0f);
#endif
}

} // namespace editor

