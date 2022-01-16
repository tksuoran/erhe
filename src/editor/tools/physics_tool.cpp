#include "tools/physics_tool.hpp"
#include "editor_imgui_windows.hpp"
#include "editor_tools.hpp"
#include "editor_view.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "renderers/line_renderer.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "windows/operations.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/physics/iconstraint.hpp"

#include <imgui.h>

namespace editor
{

void Physics_tool_drag_command::try_ready(Command_context& context)
{
    if (state() != State::Inactive)
    {
        return;
    }

    if (m_physics_tool.on_drag_ready())
    {
        set_ready(context);
    }
}

auto Physics_tool_drag_command::try_call(Command_context& context) -> bool
{
    if (state() == State::Inactive)
    {
        return false;
    }

    if (
        m_physics_tool.on_drag() &&
        (state() == State::Ready)
    )
    {
        set_active(context);
    }

    return state() == State::Active;
}

void Physics_tool_drag_command::on_inactive(Command_context& context)
{
    static_cast<void>(context);

    if (state() == State::Active)
    {
        m_physics_tool.end_drag();
    }
}

Physics_tool::Physics_tool()
    : erhe::components::Component{c_name}
    , m_drag_command             {*this}
{
}

Physics_tool::~Physics_tool()
{
    if (m_drag_constraint)
    {
        m_scene_root->physics_world().remove_constraint(m_drag_constraint.get());
    }
}

void Physics_tool::connect()
{
    m_line_renderer_set = get<Line_renderer_set>();
    m_pointer_context   = get<Pointer_context  >();
    m_scene_root        = get<Scene_root       >();
}

void Physics_tool::initialize_component()
{
    get<Editor_tools>()->register_tool(this);

    require<Scene_root>();

    const auto view = get<Editor_view>();
    view->register_command(&m_drag_command);
    view->bind_command_to_mouse_drag(&m_drag_command, erhe::toolkit::Mouse_button_right);

    get<Operations>()->register_active_tool(this);
}

auto Physics_tool::description() -> const char*
{
    return c_description.data();
}

void Physics_tool::tool_properties()
{
    ImGui::Text("State: %s",      c_state_str[static_cast<int>(m_drag_command.state())]);
    ImGui::Text("Mesh: %s",       m_drag_mesh ? m_drag_mesh->name().c_str() : "");
    ImGui::Text("Drag depth: %f", m_drag_depth);
    ImGui::Text("Constraint: %s", m_drag_constraint ? "yes" : "no");
    ImGui::SliderFloat("Tau",             &m_tau,             0.0f,   0.1f);
    ImGui::SliderFloat("Damping",         &m_damping,         0.0f,   1.0f);
    ImGui::SliderFloat("Impulse Clamp",   &m_impulse_clamp,   0.0f, 100.0f);
    ImGui::SliderFloat("Linear Damping",  &m_linear_damping,  0.0f,   1.0f);
    ImGui::SliderFloat("Angular Damping", &m_angular_damping, 0.0f,   1.0f);
}

void Physics_tool::end_drag()
{
    log_tools.trace(
        "Physics tool drag {} state transition to inactive\n",
        m_drag_mesh
            ? m_drag_mesh->name()
            : "(none)"
    );

    m_drag_mesh.reset();
    if (m_drag_constraint)
    {
        if (m_drag_node_physics)
        {
            m_drag_node_physics->rigid_body()->set_damping(
                m_original_linear_damping,
                m_original_angular_damping
            );
            m_drag_node_physics->rigid_body()->end_move();
            m_drag_node_physics.reset();
        }
        m_scene_root->physics_world().remove_constraint(m_drag_constraint.get());
        m_drag_constraint.reset();
    }
}

auto Physics_tool::on_drag_ready() -> bool
{
    if (
        !m_pointer_context->hovering_over_content() ||
        !m_pointer_context->position_in_viewport_window().has_value() ||
        !m_pointer_context->position_in_world().has_value()
    )
    {
        return false;
    }

    m_drag_mesh             = m_pointer_context->hover_mesh();
    m_drag_depth            = m_pointer_context->position_in_viewport_window().value().z;
    m_drag_position_in_mesh = m_drag_mesh->transform_point_from_world_to_local(
        m_pointer_context->position_in_world().value()
    );
    m_drag_node_physics     = get_physics_node(m_drag_mesh.get());

    if (
        !m_drag_node_physics ||
        (m_drag_node_physics->rigid_body()->get_motion_mode() == erhe::physics::Motion_mode::e_static)
    )
    {
        return false;
    }

    m_original_linear_damping  = m_drag_node_physics->rigid_body()->get_linear_damping();
    m_original_angular_damping = m_drag_node_physics->rigid_body()->get_angular_damping();

    m_drag_node_physics->rigid_body()->set_damping(
        m_linear_damping,
        m_angular_damping
    );

    // TODO Make this happen automatically
    if (m_drag_constraint)
    {
        m_scene_root->physics_world().remove_constraint(m_drag_constraint.get());
    }

    m_drag_constraint = erhe::physics::IConstraint::create_point_to_point_constraint_unique(
        m_drag_node_physics->rigid_body(),
        m_drag_position_in_mesh
    );
    m_drag_constraint->set_impulse_clamp(m_impulse_clamp);
    m_drag_constraint->set_damping      (m_damping);
    m_drag_constraint->set_tau          (m_tau);
    m_drag_node_physics->rigid_body()->begin_move();
    m_scene_root->physics_world().add_constraint(m_drag_constraint.get());

    log_tools.trace("Physics tool drag {} ready\n", m_drag_mesh->name());
    return true;
}

auto Physics_tool::on_drag() -> bool
{
    auto& physics_world = get<Scene_root>()->physics_world();
    if (!physics_world.is_physics_updates_enabled())
    {
        return false;
    }

    m_last_update_frame_number = m_pointer_context->frame_number();

    if (m_pointer_context->window() == nullptr)
    {
        return false;
    }

    const auto end = m_pointer_context->position_in_world(m_drag_depth);
    if (!end.has_value())
    {
        return false;
    }

    m_drag_position_start = glm::vec3{m_drag_mesh->world_from_node() * glm::vec4{m_drag_position_in_mesh, 1.0f}};
    m_drag_position_end   = end.value();

    if (m_drag_constraint)
    {
        m_drag_constraint->set_pivot_in_b(m_drag_position_end);
    }

    return true;
    //return false;
}

void Physics_tool::tool_render(const Render_context& /*context*/)
{
    ERHE_PROFILE_FUNCTION

    if (!m_drag_constraint)
    {
        return;
    }
    m_line_renderer_set->hidden.set_line_color(0xffffffffu);
    m_line_renderer_set->hidden.add_lines(
        {
            {
                m_drag_position_start,
                m_drag_position_end
            }
        },
        4.0f
    );
}

}
