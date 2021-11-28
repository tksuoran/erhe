#include "tools/selection_tool.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "tools/pointer_context.hpp"
#include "tools/trs_tool.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "scene/node_physics.hpp"

#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <imgui.h>

namespace editor
{

using namespace erhe::toolkit;

Selection_tool::Selection_tool()
    : erhe::components::Component{c_name}
{
}

Selection_tool::~Selection_tool() = default;

void Selection_tool::connect()
{
    m_scene_manager = get<Scene_manager>();
}

void Selection_tool::initialize_component()
{
    get<Editor_tools>()->register_tool(this);
}

auto Selection_tool::description() -> const char*
{
    return c_name.data();
}

auto Selection_tool::state() const -> State
{
    return m_state;
}

Selection_tool::Subcription Selection_tool::subscribe_selection_change_notification(On_selection_changed callback)
{
    const int handle = m_next_selection_change_subscription++;
    m_selection_change_subscriptions.push_back({callback, handle});
    return Subcription(this, handle);
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

void Selection_tool::cancel_ready()
{
    m_state = State::Passive;
}

auto Selection_tool::update(Pointer_context& pointer_context) -> bool
{
    if (pointer_context.priority_action != Action::select)
    {
        if (m_state != State::Passive)
        {
            cancel_ready();
        }
        return false;
    }

    if (!pointer_context.scene_view_focus ||
        !pointer_context.pointer_in_content_area())
    {
        return false;
    }
    m_hover_mesh     = pointer_context.hover_mesh;
    m_hover_position = glm::vec3{
        pointer_context.pointer_x,
        pointer_context.pointer_y,
        pointer_context.pointer_z
    };
    m_hover_content  = pointer_context.hover_content;
    m_hover_tool     = pointer_context.hover_tool;

    if (m_state == State::Passive)
    {
        if (m_hover_content && pointer_context.mouse_button[Mouse_button_left].pressed)
        {
            m_state = State::Ready;
            return true;
        }
        return false;
    }

    if (m_state == State::Passive)
    {
        return false;
    }

    if (pointer_context.mouse_button[Mouse_button_left].released)
    {
        if (pointer_context.shift)
        {
            if (!m_hover_content)
            {
                return false;
            }
            toggle_selection(m_hover_mesh, false);
            m_state = State::Passive;
            return true;
        }
        if (!m_hover_content)
        {
            clear_selection();
            m_state = State::Passive;
            return true;
        }
        toggle_selection(m_hover_mesh, true);
        m_state = State::Passive;
        return true;
    }
    return false;
}

auto Selection_tool::clear_selection() -> bool
{
    if (m_selection.empty())
    {
        return false;
    }

    for (auto item : m_selection)
    {
        VERIFY(item);
        if (!item)
        {
            continue;
        }
        item->visibility_mask() &= ~erhe::scene::Node::c_visibility_selected;
    }

    log_selection.trace("Clearing selection ({} items were selected)\n", m_selection.size());
    m_selection.clear();
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
        log_selection.warn("Trying to add empty item to selection\n");
        return false;
    }

    item->visibility_mask() |= erhe::scene::Node::c_visibility_selected;

    if (!is_in_selection(item))
    {
        log_selection.trace("Adding {} to selection\n", item->name());
        m_selection.push_back(item);
        call_selection_change_subscriptions();
        return true;
    }

    log_selection.warn("Adding {} to selection failed - was already in selection\n", item->name());
    return false;
}

auto Selection_tool::remove_from_selection(
    const std::shared_ptr<erhe::scene::Node>& item
) -> bool
{
    if (!item)
    {
        log_selection.warn("Trying to remove empty item from selection\n");
        return false;
    }

    item->visibility_mask() &= ~erhe::scene::Node::c_visibility_selected;

    const auto i = std::remove(
        m_selection.begin(),
        m_selection.end(),
        item
    );
    if (i != m_selection.end())
    {
        log_selection.trace("Removing item {} from selection\n", item->name());
        m_selection.erase(i, m_selection.end());
        call_selection_change_subscriptions();
        return true;
    }

    log_selection.info("Removing item {} from selection failed - was not in selection\n", item->name());
    return false;
}

void Selection_tool::call_selection_change_subscriptions()
{
    for (const auto& entry : m_selection_change_subscriptions)
    {
        entry.callback(m_selection);
    }
}

void Selection_tool::render(const Render_context& render_context)
{
    using namespace glm;

    if (render_context.line_renderer == nullptr)
    {
        return;
    }

    constexpr uint32_t red         = 0xff0000ffu;
    constexpr uint32_t green       = 0xff00ff00u;
    constexpr uint32_t blue        = 0xffff0000u;
    constexpr uint32_t yellow      = 0xff00ffffu;
    //constexpr uint32_t half_yellow = 0x88008888u; // premultiplied
    constexpr uint32_t white       = 0xffffffffu;
    constexpr uint32_t half_white  = 0x88888888u; // premultiplied
    Line_renderer& line_renderer = *render_context.line_renderer;
    for (auto node : m_selection)
    {
        const glm::mat4 m     {node->world_from_node()};
        const glm::vec3 O     {0.0f};
        const glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
        const glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
        const glm::vec3 axis_z{0.0f, 0.0f, 1.0f};
        line_renderer.add_lines( m, red,   {{ O, axis_x }} );
        line_renderer.add_lines( m, green, {{ O, axis_y }} );
        line_renderer.add_lines( m, blue,  {{ O, axis_z }} );
        if (is_mesh(node))
        {
            auto mesh = as_mesh(node);
            for (auto primitive : mesh->data.primitives)
            {
                if (!primitive.primitive_geometry)
                {
                    continue;
                }
                auto* primitive_geometry = primitive.primitive_geometry.get();
                const vec3 box_min = primitive_geometry->bounding_box_min;
                const vec3 box_max = primitive_geometry->bounding_box_max;
                line_renderer.add_lines(
                    node->world_from_node(),
                    yellow,
                    {
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
                    }
                );
            }
        }
        if (is_light(node))
        {
            auto light = as_light(node);
            const uint32_t light_color = ImGui::ColorConvertFloat4ToU32(
                ImVec4{
                    light->color.r,
                    light->color.g,
                    light->color.b,
                    1.0f
                }
            );
            const uint32_t half_light_color = ImGui::ColorConvertFloat4ToU32(
                ImVec4{
                    0.5f * light->color.r,
                    0.5f * light->color.g,
                    0.5f * light->color.b,
                    0.5f
                }
            );
            if (light->type == erhe::scene::Light_type::directional)
            {
                line_renderer.add_lines(m, light_color, {{ O, -light->range * axis_z }} );
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
                    }
                );
            }
            if (light->type == erhe::scene::Light_type::spot)
            {
                constexpr int   edge_count       = 6;
                constexpr float light_cone_sides = edge_count * 6;
                const float alpha   = light->outer_spot_angle;
                const float length  = light->range;
                const float apothem = length * std::tan(alpha * 0.5f);
                const float radius  = apothem / std::cos(pi<float>() / static_cast<float>(light_cone_sides));

                for (int i = 0; i < light_cone_sides; ++i)
                {
                    const float t0 = two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(light_cone_sides);
                    const float t1 = two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(light_cone_sides);
                    line_renderer.add_lines(
                        m,
                        light_color,
                        {
                            {
                                -length * axis_z 
                                + radius * std::cos(t0) * axis_x 
                                + radius * std::sin(t0) * axis_y,
                                -length * axis_z 
                                + radius * std::cos(t1) * axis_x 
                                + radius * std::sin(t1) * axis_y
                            }
                        }
                    );
                    line_renderer.add_lines(
                        m,
                        half_light_color,
                        {
                            {
                                -length * axis_z * 0.5f
                                + radius * std::cos(t0) * axis_x * 0.5f 
                                + radius * std::sin(t0) * axis_y * 0.5f,
                                -length * axis_z * 0.5f
                                + radius * std::cos(t1) * axis_x * 0.5f 
                                + radius * std::sin(t1) * axis_y * 0.5f
                            }
                        }
                    );
                }
                for (int i = 0; i < edge_count; ++i)
                {
                    const float t0 = two_pi<float>() * static_cast<float>(i) / static_cast<float>(edge_count);
                    line_renderer.add_lines(
                        m,
                        light_color,
                        {
                            {
                                O,
                                -length * axis_z 
                                + radius * std::cos(t0) * axis_x 
                                + radius * std::sin(t0) * axis_y,
                            }
                        }
                    );
                }
            }
        }
        if (is_icamera(node))
        {
            const auto icamera         = as_icamera(node).get();
            const mat4 clip_from_node  = icamera->projection()->get_projection_matrix(1.0f, render_context.viewport.reverse_depth);
            const mat4 node_from_clip  = inverse(clip_from_node);
            const mat4 world_from_clip = icamera->world_from_node() * node_from_clip;
            constexpr std::array<glm::vec3, 8> p = {
                glm::vec3{-1.0f, -1.0f, 0.0f},
                glm::vec3{ 1.0f, -1.0f, 0.0f},
                glm::vec3{ 1.0f,  1.0f, 0.0f},
                glm::vec3{-1.0f,  1.0f, 0.0f},
                glm::vec3{-1.0f, -1.0f, 1.0f},
                glm::vec3{ 1.0f, -1.0f, 1.0f},
                glm::vec3{ 1.0f,  1.0f, 1.0f},
                glm::vec3{-1.0f,  1.0f, 1.0f}
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
                }
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
                }
            );
        }
    }
}

}
