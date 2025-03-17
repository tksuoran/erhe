#include "tools/debug_visualizations.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_settings.hpp"
#include "renderers/render_context.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/transform/transform_tool.hpp"

#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_raytrace/iinstance.hpp"
#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
#   include "erhe_renderer/debug_renderer.hpp"
#endif
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
#   include "erhe_renderer/debug_renderer.hpp"
#   include "erhe_physics/iworld.hpp"
#   include <Jolt/Jolt.h>
#endif

#include <geogram/mesh/mesh_geometry.h>

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
    if (mode == Visualization_mode::All) {
        return true;
    }
    if (is_selected && (mode == Visualization_mode::Selected)) {
        return true;
    }
    if (is_hovered && (mode == Visualization_mode::Hovered)) {
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
            using namespace erhe::bit;
            if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
                m_hover_scene_view = message.scene_view;
            }
        }
    );
}

auto Debug_visualizations::get_selected_camera(const Render_context& render_context) -> std::shared_ptr<erhe::scene::Camera>
{
    const auto* scene     = render_context.get_scene();
    const auto& selection = m_context.selection->get_selection();

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
    erhe::renderer::Scoped_line_renderer line_renderer = render_context.get_line_renderer(2, true, true);

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

    for (const auto& primitive : mesh->get_primitives()) {
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
        }
        if (m_selection_box || (box_smaller && !m_selection_sphere)) {
            line_renderer.set_thickness(m_selection_major_width);
            line_renderer.add_cube(
                node->world_from_node(),
                m_selection_major_color,
                buffer_mesh.bounding_box.min - glm::vec3{m_gap, m_gap, m_gap},
                buffer_mesh.bounding_box.max + glm::vec3{m_gap, m_gap, m_gap}
            );
        }
        if (!box_smaller) {
            m_selection_bounding_volume.add_sphere(
                node->world_from_node(),
                buffer_mesh.bounding_sphere.center,
                buffer_mesh.bounding_sphere.radius
            );
        }
        if (m_selection_sphere || (!box_smaller && !m_selection_box)) {
            if (used_camera) {
                line_renderer.add_sphere(
                    node->world_from_node_transform(),
                    m_selection_major_color,
                    m_selection_minor_color,
                    m_selection_major_width,
                    m_selection_minor_width,
                    buffer_mesh.bounding_sphere.center,
                    buffer_mesh.bounding_sphere.radius + m_gap,
                    &camera_world_from_node_transform,
                    m_sphere_step_count
                );
            }
        }
    }
}

void Debug_visualizations::skin_visualization(const Render_context& render_context, erhe::scene::Skin& skin)
{
    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Scoped_line_renderer line_renderer = render_context.get_line_renderer(2, true, true);

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

    using namespace erhe::bit;
    if (!test_all_rhs_bits_set(light->get_flag_bits(), erhe::Item_flags::show_debug_visualizations)) {
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
    const auto shadow_render_node = m_context.editor_rendering->get_shadow_node_for_view(context.render_context.scene_view);
    if (!shadow_render_node) {
        return;
    }

    auto&                                render_context = context.render_context;
    erhe::renderer::Scoped_line_renderer line_renderer  = render_context.get_line_renderer(3, true, true);
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

void Debug_visualizations::point_light_visualization(const Light_visualization_context& context)
{
    const auto* node = context.light->get_node();
    if (node == nullptr) {
        return;
    }
    auto&                                render_context = context.render_context;
    erhe::renderer::Scoped_line_renderer line_renderer  = render_context.get_line_renderer(2, true, true);

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

    erhe::renderer::Scoped_line_renderer line_renderer = context.render_context.get_line_renderer(2, true, true);
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

    using namespace erhe::bit;
    if (!test_all_rhs_bits_set(camera->get_flag_bits(), erhe::Item_flags::show_debug_visualizations)) {
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

    erhe::renderer::Scoped_line_renderer line_renderer = render_context.get_line_renderer(2, true, true);
    line_renderer.set_thickness(m_camera_visualization_width);
    line_renderer.add_cube(
        world_from_clip,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, //// camera->get_wireframe_color(),
        clip_min_corner,
        clip_max_corner,
        true
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
    erhe::renderer::Scoped_line_renderer line_renderer = context.get_line_renderer(2, true, true);
    const auto& selection = m_context.selection->get_selection();

    m_selection_bounding_volume = erhe::math::Bounding_volume_combiner{}; // reset
    for (const auto& item : selection) {
        const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (node) {
            if (node->get_scene() != scene) {
                continue;
            }
            if (m_node_axis_visualization == Visualization_mode::Selected) {
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

    if (m_selection_bounding_volume.get_element_count() > 1) {
        if (m_selection_bounding_points_visible) {
            const erhe::scene::Camera* camera                = context.camera;
            const auto                 projection_transforms = camera->projection_transforms(context.viewport);
            const glm::mat4            clip_from_world       = projection_transforms.clip_from_world.get_matrix();

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

        erhe::math::Bounding_box    selection_bounding_box;
        erhe::math::Bounding_sphere selection_bounding_sphere;
        erhe::math::calculate_bounding_volume(m_selection_bounding_volume, selection_bounding_box, selection_bounding_sphere);
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

void Debug_visualizations::physics_nodes_visualization(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Scoped_line_renderer line_renderer = context.get_line_renderer(2, true, true);

    const auto& scene_root = context.scene_view.get_scene_root();
    const auto* camera     = context.camera;
    if (!scene_root || (camera == nullptr)) {
        return;
    }

    const auto      projection_transforms = camera->projection_transforms(context.viewport);
    const glm::mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();

    for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
        for (const auto& mesh : layer->meshes) {
            if (!should_visualize(m_physics_visualization, mesh)) {
                continue;
            }
            const auto* node = mesh->get_node();
            if (node == nullptr) {
                continue;
            }

            const auto& node_physics = get_node_physics(node);
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
                1.0f
            );
            const auto label_text = "<" + node->describe() + ">"; // node_physics->describe();
            const glm::vec2 label_size = m_context.text_renderer->measure(label_text).size();
            const glm::vec3 p3_in_window_z_negated{
                 p3_in_window.x - label_size.x * 0.5,
                 p3_in_window.y - label_size.y * 0.5,
                -p3_in_window.z
            };
            glm::vec4 label_text_color{0.3f, 1.0f, 0.3f, 1.0f};
            const uint32_t text_color = erhe::math::convert_float4_to_uint32(label_text_color);

            m_context.text_renderer->print(
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

#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
    Editor_context& editor_context = context.editor_context;
    if (editor_context.editor_settings->physics.debug_draw) {
        glm::vec4 camera_position = camera->get_node()->position_in_world();
        const JPH::Vec3 camera_position_jolt{camera_position.x, camera_position.y, camera_position.z};
        editor_context.debug_renderer->SetCameraPos(camera_position_jolt);
        erhe::physics::IWorld& world = scene_root->get_physics_world();
        world.debug_draw(*m_context.debug_renderer);
    }
#endif
}

void Debug_visualizations::raytrace_nodes_visualization(const Render_context& context)
{
    if (m_raytrace_visualization == Visualization_mode::None) {
        return;
    }

    const auto& scene_root = context.scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }

    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Scoped_line_renderer line_renderer = context.get_line_renderer(2, true, true);

    const glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    const glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};

    for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
        for (const auto& mesh : layer->meshes) {
            const auto* node = mesh->get_node();
            if (node == nullptr) {
                continue;
            }
            if (!should_visualize(m_raytrace_visualization, node)) {
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
    const auto      projection_transforms = camera->projection_transforms(context.viewport);
    const glm::mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();
    const glm::mat4 world_from_node       = node->world_from_node();

    erhe::renderer::Scoped_line_renderer line_renderer = context.get_line_renderer(2, true, true);

    erhe::scene::Mesh* hovered_scene_mesh{nullptr};
    if (m_hover_scene_view != nullptr) {
        const Hover_entry& content_hover = m_hover_scene_view->get_hover(Hover_entry::content_slot);
        hovered_scene_mesh = content_hover.scene_mesh;
    }

    for (erhe::primitive::Primitive& primitive : scene_mesh->get_mutable_primitives()) {
        if (!primitive.render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive.render_shape->get_geometry();
        if (!geometry) {
            continue;
        }

        const bool             is_mesh_hovered  = scene_mesh == hovered_scene_mesh;
        const bool             is_mesh_selected = scene_mesh->is_selected();
        const GEO::Mesh&       geo_mesh         = geometry->get_mesh();
        const Mesh_attributes& attributes       = geometry->get_attributes();

        if (should_visualize(m_vertex_labels, is_mesh_selected, is_mesh_hovered)) {
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
                const glm::vec3 p = p0 + m_vertex_label_line_length * n;

                line_renderer.set_thickness(m_vertex_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_vertex_label_line_color,
                    { { p0, p } }
                );

                const std::string label_text = m_vertex_positions ? fmt::format("{}: {}", vertex, p0) : fmt::format("{}", vertex);
                const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_vertex_label_text_color);
                label(context, clip_from_world, world_from_node, p, text_color, label_text);
                if (++label_count >= m_max_labels) {
                    break;
                }
            }
        }

        //auto polygon_normals = geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
        //if (polygon_normals == nullptr) {
        //    geometry->compute_polygon_normals();
        //    polygon_normals = geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
        //}

        if (should_visualize(m_edge_labels, is_mesh_selected, is_mesh_hovered)) {
            int label_count = 0;
            const float t = m_edge_label_line_length;
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
                const GEO::vec3f p  = p0 + m_edge_label_text_offset * n;

                line_renderer.set_thickness(m_edge_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_edge_label_line_color,
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
                const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_edge_label_text_color);
                label(context, clip_from_world, world_from_node, to_glm_vec3(p), text_color, label_text);
                if (++label_count >= m_max_labels) {
                    break;
                }
            }
        }

        if (should_visualize(m_facet_labels, is_mesh_selected, is_mesh_hovered)) {
            int label_count = 0;
            for (GEO::index_t facet : geo_mesh.facets) {
                if (!attributes.facet_centroid.has(facet)) {
                    continue;
                }
                const GEO::vec3f p = attributes.facet_centroid.get(facet);
                const GEO::vec3f n = GEO::normalize(mesh_facet_normalf(geo_mesh, facet));
                const GEO::vec3f l = p + m_facet_label_line_length * n;

                line_renderer.set_thickness(m_facet_label_line_width);
                line_renderer.add_lines(
                    world_from_node,
                    m_facet_label_line_color,
                    {{ to_glm_vec3(p), to_glm_vec3(l) }}
                );

                {
                    const std::string label_text = fmt::format("{}", facet);
                    const glm::vec4   p4_in_node = glm::vec4{to_glm_vec3(p) + m_facet_label_line_length * to_glm_vec3(n), 1.0f};
                    const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_facet_label_text_color);

                    label(context, clip_from_world, world_from_node, p4_in_node, text_color, label_text);
                }

                if (should_visualize(m_corner_labels, is_mesh_selected, is_mesh_hovered)) {
                    for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                        const GEO::index_t vertex      = geo_mesh.facet_corners.vertex(corner);
                        const GEO::vec3f   corner_p    = get_pointf(geo_mesh.vertices, vertex);
                        const GEO::vec3f   to_centroid = GEO::normalize(l - corner_p);
                        const GEO::vec3f   label_p     = corner_p + m_corner_label_line_length * to_centroid;

                        line_renderer.set_thickness(m_corner_label_line_width);
                        line_renderer.add_lines(
                            world_from_node,
                            m_corner_label_line_color,
                            {{ to_glm_vec3(corner_p), to_glm_vec3(label_p) }}
                        );

                        const std::string label_text = fmt::format("{}", corner);
                        const uint32_t    text_color = erhe::math::convert_float4_to_uint32(m_corner_label_text_color);
                        label(context, clip_from_world, world_from_node, to_glm_vec3(label_p), text_color, label_text);
                        if (++label_count >= m_max_labels) {
                            break;
                        }
                    }
                }
                if (++label_count >= m_max_labels) {
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
        1.0f
    );
    const glm::vec2 label_size = m_context.text_renderer->measure(label_text).size();
    const glm::vec3 p3_in_window_z_negated{
         p3_in_window.x - label_size.x * 0.5,
         p3_in_window.y - label_size.y * 0.5,
        -p3_in_window.z
    };
    m_context.text_renderer->print(p3_in_window_z_negated, text_color, label_text);
}


void Debug_visualizations::render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (
        m_tool_hide &&
        m_context.transform_tool->is_transform_tool_active()
    ) {
        return;
    }

    std::shared_ptr<erhe::scene::Camera> selected_camera;
    const auto& selection = m_context.selection->get_selection();
    for (const auto& item : selection) {
        if (erhe::scene::is_camera(item)) {
            selected_camera = std::dynamic_pointer_cast<erhe::scene::Camera>(item);
        }
    }

    if (m_selection) {
        selection_visualization(context);
    }

    const auto scene_root = context.scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }

    erhe::renderer::Scoped_line_renderer line_renderer = context.get_line_renderer(2, true, true);

    for (const auto& node : scene_root->get_hosted_scene()->get_flat_nodes()) {
        if (node && should_visualize(m_node_axis_visualization, node)) {
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
        if (should_visualize(m_lights, light)) {
            light_visualization(context, selected_camera, light.get());
        }
    }

    //if (m_viewport_config->debug_visualizations.camera == Visualization_mode::all)
    for (const auto& camera : scene_root->get_scene().get_cameras()) {
        if (should_visualize(m_cameras, camera)) {
            camera_visualization(context, camera.get());
        }
    }

    // Skins can be shared by multiple meshes.
    // Visualize each skin only once.
    std::set<erhe::scene::Skin*> skins;
    for (erhe::scene::Mesh_layer* layer : scene_root->layers().mesh_layers()) {
        for (const auto& mesh : layer->meshes) {
            if (mesh->skin && should_visualize(m_skins, mesh)) {
                skins.insert(mesh->skin.get());
            }
        }
    }
    for (auto* skin : skins) {
        skin_visualization(context, *skin);
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

void Debug_visualizations::imgui()
{
    ERHE_PROFILE_FUNCTION();

    Property_editor& p = m_property_editor;
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

    p.reset();
    p.add_entry("Node Axises", [this](){ make_combo("##", m_node_axis_visualization); });
    p.add_entry("Physics",     [this](){ make_combo("##", m_physics_visualization  ); });
    p.add_entry("Raytrace",    [this](){ make_combo("##", m_raytrace_visualization ); });

    p.push_group("Selection",  ImGuiTreeNodeFlags_None); //ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Selection",       [this](){ ImGui::Checkbox   ("##", &m_selection); });
    p.add_entry("Bounding points", [this](){ ImGui::Checkbox   ("##", &m_selection_bounding_points_visible); });
    if (m_selection_bounding_points_visible) {
        erhe::math::Bounding_box    selection_bounding_box;
        erhe::math::Bounding_sphere selection_bounding_sphere;
        erhe::math::calculate_bounding_volume(m_selection_bounding_volume, selection_bounding_box, selection_bounding_sphere);
        const float box_volume    = selection_bounding_box.volume();
        const float sphere_volume = selection_bounding_sphere.volume();
        p.add_entry("Box Volume",    [this, box_volume               ](){ ImGui::Text("%f", box_volume); });
        p.add_entry("Sphere radius", [this, selection_bounding_sphere](){ ImGui::Text("%f", selection_bounding_sphere.radius); });
        p.add_entry("Sphere Volume", [this, sphere_volume            ](){ ImGui::Text("%f", sphere_volume); });
    }
    p.add_entry("Selection Box",         [this](){ ImGui::Checkbox   ("##", &m_selection_box); });
    p.add_entry("Selection Sphere",      [this](){ ImGui::Checkbox   ("##", &m_selection_sphere); });
    p.add_entry("Selection Major Color", [this](){ ImGui::ColorEdit4 ("##", &m_selection_major_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Selection Minor Color", [this](){ ImGui::ColorEdit4 ("##", &m_selection_minor_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Group Major Color",     [this](){ ImGui::ColorEdit4 ("##", &m_group_selection_major_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Group Minor Color",     [this](){ ImGui::ColorEdit4 ("##", &m_group_selection_minor_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Selection Major Width", [this](){ ImGui::SliderFloat("##", &m_selection_major_width, 0.1f, 100.0f); });
    p.add_entry("Selection Minor Width", [this](){ ImGui::SliderFloat("##", &m_selection_minor_width, 0.1f, 100.0f); });
    p.pop_group();

    p.add_entry("Sphere Step Count",     [this](){ ImGui::SliderInt  ("##", &m_sphere_step_count, 1, 200); });
    p.add_entry("Gap",                   [this](){ ImGui::SliderFloat("##", &m_gap, 0.0001f, 0.1f); });
    p.add_entry("Tool Hide",             [this](){ ImGui::Checkbox   ("##", &m_tool_hide); });
    //ImGui::Checkbox   ("Raytrace",              &m_raytrace_visualization);

    p.add_entry("Lights",  [this](){ make_combo("##", m_lights); });
    p.add_entry("Cameras", [this](){ make_combo("##", m_cameras); });
    p.add_entry("Skins",   [this](){ make_combo("##", m_skins); });

    p.push_group("Annotiations", ImGuiTreeNodeFlags_None); //ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Max Labels", [this](){ ImGui::SliderInt("##", &m_max_labels, 0, 2000); });
    
    p.add_entry("Show Vertices",    [this](){ make_combo("##", m_vertex_labels); });
    p.add_entry("Vertex Positions", [this](){ ImGui::Checkbox("##", &m_vertex_positions); });
    p.add_entry("Show Facets",      [this](){ make_combo("##", m_facet_labels ); });
    p.add_entry("Show Edges",       [this](){ make_combo("##", m_edge_labels  ); });
    p.add_entry("Show Corners",     [this](){ make_combo("##", m_corner_labels); });

    p.push_group("Style", ImGuiTreeNodeFlags_None); //ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Vertex Label Text Color",  [this](){ ImGui::ColorEdit4 ("##", &m_vertex_label_text_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Vertex Label Line Color",  [this](){ ImGui::ColorEdit4 ("##", &m_vertex_label_line_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Vertex Label Line Width",  [this](){ ImGui::SliderFloat("##", &m_vertex_label_line_width,   0.0f, 4.0f); });
    p.add_entry("Vertex Label Line Length", [this](){ ImGui::SliderFloat("##", &m_vertex_label_line_length,  0.0f, 1.0f); });
    p.add_entry("Edge Label Text Color",    [this](){ ImGui::ColorEdit4 ("##", &m_edge_label_text_color.x,   ImGuiColorEditFlags_Float); });
    p.add_entry("Edge Label Text Offset",   [this](){ ImGui::SliderFloat("##", &m_edge_label_text_offset,    0.0f, 1.0f); });
    p.add_entry("Edge Label Line Color",    [this](){ ImGui::ColorEdit4 ("##", &m_edge_label_line_color.x,   ImGuiColorEditFlags_Float); });
    p.add_entry("Edge Label Line Width",    [this](){ ImGui::SliderFloat("##", &m_edge_label_line_width,     0.0f, 4.0f); });
    p.add_entry("Edge Label Line Length",   [this](){ ImGui::SliderFloat("##", &m_edge_label_line_length,    0.0f, 1.0f); });
    p.add_entry("Facet Label Text Color",   [this](){ ImGui::ColorEdit4 ("##", &m_facet_label_text_color.x,  ImGuiColorEditFlags_Float); });
    p.add_entry("Facet Label Line Color",   [this](){ ImGui::ColorEdit4 ("##", &m_facet_label_line_color.x,  ImGuiColorEditFlags_Float); });
    p.add_entry("Facet Label Line Width",   [this](){ ImGui::SliderFloat("##", &m_facet_label_line_width,    0.0f, 4.0f); });
    p.add_entry("Facet Label Line Length",  [this](){ ImGui::SliderFloat("##", &m_facet_label_line_length,   0.0f, 1.0f); });
    p.add_entry("Corner Label Text Color",  [this](){ ImGui::ColorEdit4 ("##", &m_corner_label_text_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Corner Label Line Color",  [this](){ ImGui::ColorEdit4 ("##", &m_corner_label_line_color.x, ImGuiColorEditFlags_Float); });
    p.add_entry("Corner Label Line Width",  [this](){ ImGui::SliderFloat("##", &m_corner_label_line_width,   0.0f, 4.0f); });
    p.add_entry("Corner Label Line Length", [this](){ ImGui::SliderFloat("##", &m_corner_label_line_length,  0.0f, 1.0f); });
    p.pop_group();
    p.pop_group();
    p.show_entries();
}

} // namespace editor

