#include "tools/selection_tool.hpp"

#include "log.hpp"
#include "editor_rendering.hpp"
#include "operations/compound_operation.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"
#include "windows/viewport_config.hpp"

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

Range_selection::Range_selection(Selection_tool& selection_tool)
    : m_selection_tool{selection_tool}
{
}

void Range_selection::set_terminator(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    if (!m_primary_terminator)
    {
        log_selection->trace("setting primary terminator to {} {}", node->node_type(), node->name());
        m_primary_terminator = node;
        m_edited = true;
        return;
    }
    if (m_secondary_terminator == node)
    {
        return;
    }
    log_selection->trace("setting secondary terminator to {} {}", node->node_type(), node->name());
    m_secondary_terminator = node;
    m_edited = true;
}

void Range_selection::entry(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    m_entries.push_back(node);
}

void Range_selection::begin()
{
    m_edited = false;
    m_entries.clear();
}

void Range_selection::end()
{
    if (m_entries.empty() || !m_edited || !m_primary_terminator || !m_secondary_terminator)
    {
        m_entries.clear();
        return;
    }
    log_selection->trace("setting selection since range was modified");

    std::vector<std::shared_ptr<erhe::scene::Node>> selection;
    bool                                            between_terminators{false};
    for (const auto& node : m_entries)
    {
        if (
            (node == m_primary_terminator) ||
            (node == m_secondary_terminator)
        )
        {
            log_selection->trace("    ! {} {} {}", node->node_type(), node->name(), node->get_id());
            selection.push_back(node);
            between_terminators = !between_terminators;
            continue;
        }
        if (between_terminators)
        {
            log_selection->trace("    + {} {} {}", node->node_type(), node->name(), node->get_id());
            selection.push_back(node);
        }
        else
        {
            log_selection->trace("    - {} {} {}", node->node_type(), node->name(), node->get_id());
        }
    }
    if (selection.empty())
    {
        m_entries.clear();
        return;
    }

    m_selection_tool.set_selection(selection);
    m_entries.clear();
}

void Range_selection::reset()
{
    log_selection->trace("resetting range selection");
    if (m_primary_terminator && m_secondary_terminator)
    {
        m_selection_tool.clear_selection();
    }
    m_primary_terminator.reset();
    m_secondary_terminator.reset();
}

void Selection_tool_select_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (m_selection_tool.mouse_select_try_ready())
    {
        set_ready(context);
    }
}

auto Selection_tool_select_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (state() != erhe::application::State::Ready)
    {
        return false;
    }

    const bool consumed = m_selection_tool.on_mouse_select();
    set_inactive(context);
    return consumed;
}

auto Selection_tool_delete_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_selection_tool.delete_selection();
}

auto Selection_tool::delete_selection() -> bool
{
    if (m_selection.empty())
    {
        return false;
    }

    const auto scene_root = get<Scene_root>();
    Compound_operation::Parameters compound_parameters;
    for (auto& node : m_selection)
    {
        // TODO Handle all node types
        if (is_mesh(node))
        {
            const auto mesh = as_mesh(node);
            compound_parameters.operations.push_back(
                std::make_shared<Mesh_insert_remove_operation>(
                    Mesh_insert_remove_operation::Parameters{
                        .scene          = scene_root->scene(),
                        .layer          = *scene_root->content_layer(),
                        .physics_world  = scene_root->physics_world(),
                        .raytrace_scene = scene_root->raytrace_scene(),
                        .mesh           = mesh,
                        .node_physics   = get_physics_node(mesh.get()),
                        .node_raytrace  = get_raytrace(mesh.get()),
                        .parent         = mesh->parent().lock(),
                        .mode           = Scene_item_operation::Mode::remove,
                    }
                )
            );
        }
        else if (is_light(node))
        {
            const auto light = as_light(node);
            compound_parameters.operations.push_back(
                std::make_shared<Light_insert_remove_operation>(
                    Light_insert_remove_operation::Parameters{
                        .scene          = scene_root->scene(),
                        .layer          = *scene_root->light_layer(),
                        .light          = light,
                        .parent         = light->parent().lock(),
                        .mode           = Scene_item_operation::Mode::remove,
                    }
                )
            );
        }
        else if (is_camera(node))
        {
            const auto camera = as_camera(node);
            compound_parameters.operations.push_back(
                std::make_shared<Camera_insert_remove_operation>(
                    Camera_insert_remove_operation::Parameters{
                        .scene          = scene_root->scene(),
                        .camera         = camera,
                        .parent         = camera->parent().lock(),
                        .mode           = Scene_item_operation::Mode::remove,
                    }
                )
            );
        }
    }
    if (compound_parameters.operations.empty())
    {
        return false;
    }

    const auto op = std::make_shared<Compound_operation>(std::move(compound_parameters));
    get<Operation_stack>()->push(op);
    return true;
}

auto Selection_tool::range_selection() -> Range_selection&
{
    return m_range_selection;
}

Selection_tool::Selection_tool()
    : erhe::components::Component{c_label}
    , m_select_command           {*this}
    , m_delete_command           {*this}
    , m_range_selection          {*this}
{
}

Selection_tool::~Selection_tool() noexcept
{
}

void Selection_tool::declare_required_components()
{
    require<Tools>();
    require<erhe::application::View >();
}

void Selection_tool::initialize_component()
{
    get<Tools>()->register_tool(this);

    const auto view = get<erhe::application::View>();

    view->register_command           (&m_select_command);
    view->register_command           (&m_delete_command);
    view->bind_command_to_mouse_click(&m_select_command, erhe::toolkit::Mouse_button_left);
    view->bind_command_to_key        (&m_delete_command, erhe::toolkit::Key_delete, true);
}

void Selection_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_pointer_context   = get<Pointer_context  >();
    m_scene_root        = get<Scene_root       >();
    m_viewport_config   = get<Viewport_config  >();
}

auto Selection_tool::description() -> const char*
{
    return c_title.data();
}

auto Selection_tool::subscribe_selection_change_notification(
    On_selection_changed callback
) -> Selection_tool::Subcription
{
    const int handle = m_next_selection_change_subscription++;
    m_selection_change_subscriptions.push_back({callback, handle});
    return Subcription(this, handle);
}

auto Selection_tool::selection() const -> const Selection&
{
    return m_selection;
}

template <typename T>
[[nodiscard]] auto is_in(
    const T&              item,
    const std::vector<T>& items
) -> bool
{
    return std::find(
        items.begin(),
        items.end(),
        item
    ) != items.end();
}

void Selection_tool::set_selection(const Selection& selection)
{
    for (auto& node : m_selection)
    {
        if (
            node->is_selected() &&
            !is_in(node, selection)
        )
        {
            const auto mask = node->get_visibility_mask();
            node->set_visibility_mask(mask & ~erhe::scene::Node_visibility::selected);
        }
    }
    for (auto& node : selection)
    {
        const auto mask = node->get_visibility_mask();
        node->set_visibility_mask(mask | erhe::scene::Node_visibility::selected);
    }
    m_selection = selection;
    call_selection_change_subscriptions();
}

void Selection_tool::unsubscribe_selection_change_notification(int handle)
{
    m_selection_change_subscriptions.erase(
        std::remove_if(
            m_selection_change_subscriptions.begin(),
            m_selection_change_subscriptions.end(),
            [=](Subscription_entry& entry) -> bool
            {
                return entry.handle == handle;
            }
        ),
        m_selection_change_subscriptions.end()
    );
}

auto Selection_tool::mouse_select_try_ready() -> bool
{
    if (m_pointer_context->window() == nullptr)
    {
        return false;
    }

    const auto& content = m_pointer_context->get_hover(Pointer_context::content_slot);
    const auto& tool    = m_pointer_context->get_hover(Pointer_context::tool_slot);
    m_hover_mesh    = content.mesh;
    m_hover_content = content.valid;
    m_hover_tool    = tool.valid;

    return m_hover_content;
}

auto Selection_tool::on_mouse_select() -> bool
{
    if (m_pointer_context->control_key_down())
    {
        if (m_hover_content)
        {
            toggle_selection(m_hover_mesh, false);
            return true;
        }
        return false;
    }

    if (!m_hover_content)
    {
        clear_selection();
    }
    else
    {
        m_range_selection.reset();
        toggle_selection(m_hover_mesh, true);
    }
    return true;
}

auto Selection_tool::clear_selection() -> bool
{
    if (m_selection.empty())
    {
        return false;
    }

    for (const auto& item : m_selection)
    {
        ERHE_VERIFY(item);
        if (!item)
        {
            continue;
        }
        const auto mask = item->get_visibility_mask();
        item->set_visibility_mask(mask & ~erhe::scene::Node_visibility::selected);
    }

    log_selection->trace("Clearing selection ({} items were selected)", m_selection.size());
    m_selection.clear();
    m_range_selection.reset();
    sanity_check();
    call_selection_change_subscriptions();
    return true;
}

void Selection_tool::toggle_selection(
    const std::shared_ptr<erhe::scene::Node>& item,
    const bool                                clear_others
)
{
    if (clear_others)
    {
        const bool was_selected = is_in_selection(item);

        clear_selection();
        if (!was_selected && item)
        {
            add_to_selection(item);
            m_range_selection.set_terminator(item);
        }
    }
    else if (item)
    {
        if (is_in_selection(item))
        {
            remove_from_selection(item);
        }
        else
        {
            add_to_selection(item);
        }
    }

    call_selection_change_subscriptions();
}

auto Selection_tool::is_in_selection(
    const std::shared_ptr<erhe::scene::Node>& item
) const -> bool
{
    if (!item)
    {
        return false;
    }

    return std::find(
        m_selection.begin(),
        m_selection.end(),
        item
    ) != m_selection.end();
}

auto Selection_tool::add_to_selection(
    const std::shared_ptr<erhe::scene::Node>& item
) -> bool
{
    if (!item)
    {
        log_selection->warn("Trying to add empty item to selection");
        return false;
    }

    const auto mask = item->get_visibility_mask();
    item->set_visibility_mask(
        mask | erhe::scene::Node_visibility::selected
    );

    if (!is_in_selection(item))
    {
        log_selection->trace("Adding {} to selection", item->name());
        m_selection.push_back(item);
        call_selection_change_subscriptions();
        return true;
    }

    log_selection->warn("Adding {} to selection failed - was already in selection", item->name());
    return false;
}

auto Selection_tool::remove_from_selection(
    const std::shared_ptr<erhe::scene::Node>& item
) -> bool
{
    if (!item)
    {
        log_selection->warn("Trying to remove empty item from selection");
        return false;
    }

    const auto mask = item->get_visibility_mask();
    item->set_visibility_mask(mask & ~erhe::scene::Node_visibility::selected);

    const auto i = std::remove(
        m_selection.begin(),
        m_selection.end(),
        item
    );
    if (i != m_selection.end())
    {
        log_selection->trace("Removing item {} from selection", item->name());
        m_selection.erase(i, m_selection.end());
        call_selection_change_subscriptions();
        return true;
    }

    log_selection->info("Removing item {} from selection failed - was not in selection", item->name());
    return false;
}

void Selection_tool::update_selection_from_node(
    const std::shared_ptr<erhe::scene::Node>& node,
    const bool                                added
)
{
    if (node->is_selected() && added)
    {
        if (!is_in(node, m_selection))
        {
            m_selection.push_back(node);
            call_selection_change_subscriptions();
        }
    }
    else
    {
        if (is_in(node, m_selection))
        {
            const auto i = std::remove(
                m_selection.begin(),
                m_selection.end(),
                node
            );
            if (i != m_selection.end())
            {
                m_selection.erase(i, m_selection.end());
                call_selection_change_subscriptions();
            }
        }
    }
}

void Selection_tool::sanity_check()
{
    const auto& scene = m_scene_root->scene();
    std::size_t error_count{0};
    for (const auto& node : scene.flat_node_vector)
    {
        if (node->is_selected() && !is_in(node, m_selection))
        {
            log_selection->error("Node has selection flag set without being in selection");
            ++error_count;
        }
        else if (!node->is_selected() && is_in(node, m_selection))
        {
            log_selection->error("Node does not have selection flag set while being in selection");
            ++error_count;
        }
    }
    if (error_count > 0)
    {
        log_selection->error("Selection errors: {}", error_count);
    }
}

void Selection_tool::call_selection_change_subscriptions() const
{
    for (const auto& entry : m_selection_change_subscriptions)
    {
        entry.callback(m_selection);
    }
}

namespace
{

auto sign(const float x) -> float
{
    return x < 0.0f ? -1.0f : 1.0f;
}

}

void Selection_tool::tool_render(
    const Render_context& context
)
{
    if (m_line_renderer_set == nullptr)
    {
        return;
    }

    constexpr uint32_t red         = 0xff0000ffu;
    constexpr uint32_t green       = 0xff00ff00u;
    constexpr uint32_t blue        = 0xffff0000u;
    constexpr uint32_t yellow      = 0xff00ffffu;
    constexpr uint32_t half_yellow = 0x88008888u;
    constexpr uint32_t white       = 0xffffffffu;
    constexpr uint32_t half_white  = 0x88888888u; // premultiplied
    constexpr float    thickness   = 10.0f;
    auto& line_renderer = m_line_renderer_set->hidden;
    for (const auto& node : m_selection)
    {
        const mat4 m     {node->world_from_node()};
        const vec3 O     {0.0f};
        const vec3 axis_x{1.0f, 0.0f, 0.0f};
        const vec3 axis_y{0.0f, 1.0f, 0.0f};
        const vec3 axis_z{0.0f, 0.0f, 1.0f};
        line_renderer.add_lines( m, red,   {{ O, axis_x }}, thickness );
        line_renderer.add_lines( m, green, {{ O, axis_y }}, thickness );
        line_renderer.add_lines( m, blue,  {{ O, axis_z }}, thickness );

        if (is_mesh(node))
        {
            const auto& mesh = as_mesh(node);
            for (const auto& primitive : mesh->mesh_data.primitives)
            {
                if (!primitive.source_geometry)
                {
                    continue;
                }
                const auto& primitive_geometry = primitive.gl_primitive_geometry;
                const vec3 box_min = primitive_geometry.bounding_box.min;
                const vec3 box_max = primitive_geometry.bounding_box.max;
                line_renderer.add_lines(
                    node->world_from_node(),
                    yellow,
                    {
                        { vec3{box_min.x, box_min.y, box_min.z}, vec3{box_max.x, box_min.y, box_min.z} },
		                { vec3{box_max.x, box_min.y, box_min.z}, vec3{box_max.x, box_max.y, box_min.z} },
		                { vec3{box_max.x, box_max.y, box_min.z}, vec3{box_min.x, box_max.y, box_min.z} },
		                { vec3{box_min.x, box_max.y, box_min.z}, vec3{box_min.x, box_min.y, box_min.z} },
		                { vec3{box_min.x, box_min.y, box_min.z}, vec3{box_min.x, box_min.y, box_max.z} },
		                { vec3{box_max.x, box_min.y, box_min.z}, vec3{box_max.x, box_min.y, box_max.z} },
		                { vec3{box_max.x, box_max.y, box_min.z}, vec3{box_max.x, box_max.y, box_max.z} },
		                { vec3{box_min.x, box_max.y, box_min.z}, vec3{box_min.x, box_max.y, box_max.z} },
		                { vec3{box_min.x, box_min.y, box_max.z}, vec3{box_max.x, box_min.y, box_max.z} },
		                { vec3{box_max.x, box_min.y, box_max.z}, vec3{box_max.x, box_max.y, box_max.z} },
		                { vec3{box_max.x, box_max.y, box_max.z}, vec3{box_min.x, box_max.y, box_max.z} },
                        { vec3{box_min.x, box_max.y, box_max.z}, vec3{box_min.x, box_min.y, box_max.z} }
                    },
                    thickness
                );
                line_renderer.add_sphere(
                    node->world_from_node(),
                    half_yellow,
                    primitive_geometry.bounding_sphere.center,
                    primitive_geometry.bounding_sphere.radius,
                    thickness
                );
            }
        }

        if (
            is_light(node) &&
            (m_viewport_config->debug_visualizations.light == Visualization_mode::selected)
        )
        {
            const auto& light = as_light(node);
            const uint32_t light_color = erhe::toolkit::convert_float4_to_uint32(light->color);
            const uint32_t half_light_color = erhe::toolkit::convert_float4_to_uint32(
                glm::vec4{0.5f * light->color, 0.5f}
            );
            if (light->type == erhe::scene::Light_type::directional)
            {
                line_renderer.add_lines(m, light_color, {{ O, -light->range * axis_z }}, thickness );
            }
            if (light->type == erhe::scene::Light_type::point)
            {
                constexpr float scale = 0.5f;
                const auto nnn = scale * normalize(-axis_x - axis_y - axis_z);
                const auto nnp = scale * normalize(-axis_x - axis_y + axis_z);
                const auto npn = scale * normalize(-axis_x + axis_y - axis_z);
                const auto npp = scale * normalize(-axis_x + axis_y + axis_z);
                const auto pnn = scale * normalize( axis_x - axis_y - axis_z);
                const auto pnp = scale * normalize( axis_x - axis_y + axis_z);
                const auto ppn = scale * normalize( axis_x + axis_y - axis_z);
                const auto ppp = scale * normalize( axis_x + axis_y + axis_z);
                line_renderer.add_lines(
                    m,
                    light_color,
                    {
                        { -scale * axis_x, scale * axis_x},
                        { -scale * axis_y, scale * axis_y},
                        { -scale * axis_z, scale * axis_z},
                        { nnn, ppp },
                        { nnp, ppn },
                        { npn, pnp },
                        { npp, pnn }
                    },
                    thickness
                );
            }
            if (light->type == erhe::scene::Light_type::spot)
            {
                constexpr int   edge_count       = 200;
                constexpr float light_cone_sides = edge_count * 6;
                const float outer_alpha   = light->outer_spot_angle;
                const float inner_alpha   = light->inner_spot_angle;
                const float length        = light->range;
                const float outer_apothem = length * std::tan(outer_alpha * 0.5f);
                const float inner_apothem = length * std::tan(inner_alpha * 0.5f);
                const float outer_radius  = outer_apothem / std::cos(glm::pi<float>() / static_cast<float>(light_cone_sides));
                const float inner_radius  = inner_apothem / std::cos(glm::pi<float>() / static_cast<float>(light_cone_sides));

                const vec3 view_position = node->transform_point_from_world_to_local(context.camera->position_in_world());
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
                        light_color,
                        {
                            {
                                -length * axis_z
                                + outer_radius * std::cos(t0) * axis_x
                                + outer_radius * std::sin(t0) * axis_y,
                                -length * axis_z
                                + outer_radius * std::cos(t1) * axis_x
                                + outer_radius * std::sin(t1) * axis_y
                            }
                        },
                        thickness
                    );
                    line_renderer.add_lines(
                        m,
                        half_light_color,
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
                        },
                        thickness
                    );
                }
                line_renderer.add_lines(
                    m,
                    half_light_color,
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
                    },
                    thickness
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
                    line_renderer.add_lines(m, light_color, { { O, edge.p } }, thickness);
                    //line_renderer.add_lines(m, red,   { { edge.p, edge.p + 0.1f * edge.n } }, thickness);
                    //line_renderer.add_lines(m, green, { { edge.p, edge.p + 0.1f * edge.t } }, thickness);
                    //line_renderer.add_lines(m, blue,  { { edge.p, edge.p + 0.1f * edge.b } }, thickness);
                }
            }
        }

        if (
            is_icamera(node) &&
            (
                (
                    !is_light(node) &&
                    (m_viewport_config->debug_visualizations.camera == Visualization_mode::selected)
                ) ||
                (
                    is_light(node) &&
                    (m_viewport_config->debug_visualizations.light_camera == Visualization_mode::selected)
                )
            )

        )
        {
            const auto& icamera         = as_icamera(node).get();
            const mat4  clip_from_node  = icamera->projection()->get_projection_matrix(1.0f, context.viewport.reverse_depth);
            const mat4  node_from_clip  = inverse(clip_from_node);
            const mat4  world_from_clip = icamera->world_from_node() * node_from_clip;
            constexpr std::array<vec3, 8> p = {
                vec3{-1.0f, -1.0f, 0.0f},
                vec3{ 1.0f, -1.0f, 0.0f},
                vec3{ 1.0f,  1.0f, 0.0f},
                vec3{-1.0f,  1.0f, 0.0f},
                vec3{-1.0f, -1.0f, 1.0f},
                vec3{ 1.0f, -1.0f, 1.0f},
                vec3{ 1.0f,  1.0f, 1.0f},
                vec3{-1.0f,  1.0f, 1.0f}
            };

            line_renderer.add_lines(
                world_from_clip,
                white,
                {
                    // near plane
                    { p[0], p[1] },
                    { p[1], p[2] },
                    { p[2], p[3] },
                    { p[3], p[0] },

                    // far plane
                    { p[4], p[5] },
                    { p[5], p[6] },
                    { p[6], p[7] },
                    { p[7], p[4] },

                    // near to far
                    { p[0], p[4] },
                    { p[1], p[5] },
                    { p[2], p[6] },
                    { p[3], p[7] }
                },
                thickness
            );
            line_renderer.add_lines(
                world_from_clip,
                half_white,
                {
                    // near to far middle
                    { 0.5f * p[0] + 0.5f * p[1], 0.5f * p[4] + 0.5f * p[5] },
                    { 0.5f * p[1] + 0.5f * p[2], 0.5f * p[5] + 0.5f * p[6] },
                    { 0.5f * p[2] + 0.5f * p[3], 0.5f * p[6] + 0.5f * p[7] },
                    { 0.5f * p[3] + 0.5f * p[0], 0.5f * p[7] + 0.5f * p[4] },

                    // near+far/2 plane
                    { 0.5f * p[0] + 0.5f * p[4], 0.5f * p[1] + 0.5f * p[5] },
                    { 0.5f * p[1] + 0.5f * p[5], 0.5f * p[2] + 0.5f * p[6] },
                    { 0.5f * p[2] + 0.5f * p[6], 0.5f * p[3] + 0.5f * p[7] },
                    { 0.5f * p[3] + 0.5f * p[7], 0.5f * p[0] + 0.5f * p[4] },
                },
                thickness
            );
        }
    }
}

}
