#include "tools/debug_visualizations.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/viewport_config.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/application/time.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"

namespace editor
{

using glm::mat4;
using glm::vec3;
using glm::vec4;

Debug_visualizations::Debug_visualizations()
    : erhe::components::Component{c_label}
{
}

Debug_visualizations::~Debug_visualizations() noexcept
{
}

void Debug_visualizations::declare_required_components()
{
    require<Tools>();
    require<erhe::application::View >();
}

void Debug_visualizations::initialize_component()
{
    get<Tools>()->register_tool(this);

    const auto view = get<erhe::application::View>();
}

void Debug_visualizations::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_selection_tool    = get<Selection_tool >();
    m_viewport_config   = get<Viewport_config>();
}

auto Debug_visualizations::description() -> const char*
{
    return c_title.data();
}

namespace
{

auto sign(const float x) -> float
{
    return x < 0.0f ? -1.0f : 1.0f;
}

constexpr uint32_t red         = 0xff0000ffu;
constexpr uint32_t green       = 0xff00ff00u;
constexpr uint32_t blue        = 0xffff0000u;
constexpr uint32_t yellow      = 0xff00ffffu;
constexpr uint32_t half_yellow = 0x88008888u;
constexpr uint32_t white       = 0xffffffffu;

constexpr mat4 I              { 1.0f};
constexpr vec3 clip_min_corner{-1.0f, -1.0f, 0.0f};
constexpr vec3 clip_max_corner{ 1.0f,  1.0f, 1.0f};
constexpr vec3 O              { 0.0f };
constexpr vec3 axis_x         { 1.0f,  0.0f, 0.0f};
constexpr vec3 axis_y         { 0.0f,  1.0f, 0.0f};
constexpr vec3 axis_z         { 0.0f,  0.0f, 1.0f};

}

void Debug_visualizations::mesh_selection_visualization(
    erhe::scene::Mesh* mesh
)
{
    auto&          line_renderer = m_line_renderer_set->hidden;
    //const uint32_t color         = is_in_selection
    //    ? 0xff00ffffu
    //    : erhe::toolkit::convert_float4_to_uint32(mesh.node_data.wireframe_color);
    const uint32_t color         = 0xff00ffffu;

    //const auto& mesh = as_mesh(node);
    for (const auto& primitive : mesh->mesh_data.primitives)
    {
        if (!primitive.source_geometry)
        {
            continue;
        }
        const auto& primitive_geometry = primitive.gl_primitive_geometry;
        if (m_viewport_config->selection_bounding_box)
        {
            line_renderer.add_cube(
                mesh->world_from_node(),
                color,
                primitive_geometry.bounding_box.min,
                primitive_geometry.bounding_box.max
            );
        }
        if (m_viewport_config->selection_bounding_sphere)
        {
            line_renderer.add_sphere(
                mesh->world_from_node(),
                color,
                primitive_geometry.bounding_sphere.center,
                primitive_geometry.bounding_sphere.radius
            );
        }
    }
}

namespace {

[[nodiscard]] auto test_bits(uint64_t mask, uint64_t test_bits) -> bool
{
    return (mask & test_bits) == test_bits;
}

}

void Debug_visualizations::light_visualization(
    const Render_context&                render_context,
    std::shared_ptr<erhe::scene::Camera> selected_camera,
    const erhe::scene::Light*            light
)
{
    if (!test_bits(light->node_data.flag_bits, erhe::scene::Node_flag_bit::show_debug_visualizations))
    {
        return;
    }

    Light_visualization_context light_context
    {
        .render_context   = render_context,
        .selected_camera  = selected_camera,
        .light            = light,
        .light_color      = erhe::toolkit::convert_float4_to_uint32(light->color),
        .half_light_color = erhe::toolkit::convert_float4_to_uint32(
            glm::vec4{0.5f * light->color, 0.5f}
        )
    };

    switch (light->type)
    {
        case erhe::scene::Light_type::directional: directional_light_visualization(light_context); break;
        case erhe::scene::Light_type::point:       point_light_visualization      (light_context); break;
        case erhe::scene::Light_type::spot:        spot_light_visualization       (light_context); break;
        default: break;
    }
}

void Debug_visualizations::directional_light_visualization(
    Light_visualization_context& context
)
{
    auto& line_renderer = m_line_renderer_set->hidden;

    using Camera                       = erhe::scene::Camera;
    using Camera_projection_transforms = erhe::scene::Camera_projection_transforms;

    const auto               shadow_renderer              = get<Shadow_renderer>();
    const Light_projections& light_projections            = shadow_renderer->light_projections();
    const auto*              light                        = context.light;
    const auto               light_projection_transforms  = light_projections.get_light_projection_transforms_for_light(light);

    if (light_projection_transforms == nullptr)
    {
        return;
    }

    const glm::mat4 world_from_light_clip   = light_projection_transforms->clip_from_world.inverse_matrix();
    const glm::mat4 world_from_light_camera = light_projection_transforms->world_from_light_camera.matrix();

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

void Debug_visualizations::point_light_visualization(Light_visualization_context& context)
{
    auto& line_renderer = m_line_renderer_set->hidden;

    constexpr float scale = 0.5f;
    const auto nnn = scale * glm::normalize(-axis_x - axis_y - axis_z);
    const auto nnp = scale * glm::normalize(-axis_x - axis_y + axis_z);
    const auto npn = scale * glm::normalize(-axis_x + axis_y - axis_z);
    const auto npp = scale * glm::normalize(-axis_x + axis_y + axis_z);
    const auto pnn = scale * glm::normalize( axis_x - axis_y - axis_z);
    const auto pnp = scale * glm::normalize( axis_x - axis_y + axis_z);
    const auto ppn = scale * glm::normalize( axis_x + axis_y - axis_z);
    const auto ppp = scale * glm::normalize( axis_x + axis_y + axis_z);
    line_renderer.add_lines(
        context.light->world_from_node(),
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

void Debug_visualizations::spot_light_visualization(Light_visualization_context& context)
{
    auto& line_renderer = m_line_renderer_set->hidden;
    const Render_context&     render_context = context.render_context;
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

    const mat4 m             = light->world_from_node();
    const vec3 view_position = light->transform_point_from_world_to_local(
        render_context.camera->position_in_world()
    );

    //auto* editor_time = get<Editor_time>();
    //const float time = static_cast<float>(editor_time->time());
    //const float half_position = (editor_time != nullptr)
    //    ? time - floor(time)
    //    : 0.5f;

    for (int i = 0; i < light_cone_sides; ++i)
    {
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
                    -length * axis_z
                    + inner_radius * std::cos(t0) * axis_x
                    + inner_radius * std::sin(t0) * axis_y,
                    -length * axis_z
                    + inner_radius * std::cos(t1) * axis_x
                    + inner_radius * std::sin(t1) * axis_y
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
    for (int i = 0; i < edge_count; ++i)
    {
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
    for (size_t i = 0; i < cone_edges.size(); ++i)
    {
        const std::size_t next_i    = (i + 1) % cone_edges.size();
        const auto&       edge      = cone_edges[i];
        const auto&       next_edge = cone_edges[next_i];
        if (sign(edge.n_dot_v) != sign(next_edge.n_dot_v))
        {
            if (std::abs(edge.n_dot_v) < std::abs(next_edge.n_dot_v))
            {
                sign_flip_edges.push_back(edge);
            }
            else
            {
                sign_flip_edges.push_back(next_edge);
            }
        }
    }

    for (auto& edge : sign_flip_edges)
    {
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
    auto& line_renderer = m_line_renderer_set->hidden;

    if (camera == render_context.camera)
    {
        return;
    }

    if (!test_bits(camera->node_data.flag_bits, erhe::scene::Node_flag_bit::show_debug_visualizations))
    {
        return;
    }

    const mat4  clip_from_node  = camera->projection()->get_projection_matrix(1.0f, render_context.viewport.reverse_depth);
    const mat4  node_from_clip  = inverse(clip_from_node);
    const mat4  world_from_clip = camera->world_from_node() * node_from_clip;

    const uint32_t color      = erhe::toolkit::convert_float4_to_uint32(camera->node_data.wireframe_color);
    //const uint32_t half_color = erhe::toolkit::convert_float4_to_uint32(
    //    vec4{0.5f * vec3{camera->node_data.wireframe_color}, 0.5f}
    //);

    line_renderer.add_cube(
        world_from_clip,
        color,
        clip_min_corner,
        clip_max_corner,
        true
    );
}

void Debug_visualizations::tool_render(
    const Render_context& context
)
{
    if (m_line_renderer_set == nullptr)
    {
        return;
    }

    auto& line_renderer = m_line_renderer_set->hidden;
    line_renderer.set_thickness(10.0f);

    std::shared_ptr<erhe::scene::Camera> selected_camera;
    const auto& selection = m_selection_tool->selection();
    for (const auto& node : selection)
    {
        if (is_camera(node))
        {
            selected_camera = as_camera(node);
        }
    }

    for (const auto& node : selection)
    {
        //const mat4 m{node->world_from_node()};
        //line_renderer.add_lines( m, red,   {{ O, axis_x }} );
        //line_renderer.add_lines( m, green, {{ O, axis_y }} );
        //line_renderer.add_lines( m, blue,  {{ O, axis_z }} );

        if (is_mesh(node))
        {
            mesh_selection_visualization(as_mesh(node).get());
        }

        if (
            is_camera(node) &&
            (m_viewport_config->debug_visualizations.camera == Visualization_mode::selected)
        )
        {
            camera_visualization(context, as_camera(node).get());
        }
    }


    const Scene_root* scene_root = context.window->scene_root();
    for (const auto& light : scene_root->layers().light()->lights)
    {
        light_visualization(context, selected_camera, light.get());
    }

    if (m_viewport_config->debug_visualizations.camera == Visualization_mode::all)
    {
        for (const auto& camera : scene_root->scene().cameras)
        {
            camera_visualization(context, camera.get());
        }
    }
}

} // namespace editor

