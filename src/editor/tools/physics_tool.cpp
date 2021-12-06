#include "tools/physics_tool.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "tools.hpp"
#include "tools/pointer_context.hpp"
#include "renderers/line_renderer.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/physics/iconstraint.hpp"

#include <imgui.h>

namespace editor
{

using namespace erhe::toolkit;

Physics_tool::Physics_tool()
    : erhe::components::Component{c_name}
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
    m_line_renderer   = get<Line_renderer>();
    m_pointer_context = get<Pointer_context>();
    m_scene_root      = get<Scene_root>();
}

void Physics_tool::initialize_component()
{
    get<Editor_tools>()->register_tool(this);
    require<Scene_root>();
}

auto Physics_tool::state() const -> State
{
    return m_state;
}

auto Physics_tool::description() -> const char*
{
    return c_description.data();
}

void Physics_tool::tool_properties()
{
    ImGui::Text("State: %s",      Tool::c_state_str[static_cast<int>(m_state)]);
    ImGui::Text("Mesh: %s",       m_drag_mesh ? m_drag_mesh->name().c_str() : "");
    ImGui::Text("Drag depth: %f", m_drag_depth);
    ImGui::Text("Constraint: %s", m_drag_constraint ? "yes" : "no");
    ImGui::SliderFloat("Tau",             &m_tau,             0.0f,  0.1f);
    ImGui::SliderFloat("Damping",         &m_damping,         0.0f,  1.0f);
    ImGui::SliderFloat("Impulse Clamp",   &m_impulse_clamp,   0.0f, 10.0f);
    ImGui::SliderFloat("Linear Damping",  &m_linear_damping,  0.0f,  1.0f);
    ImGui::SliderFloat("Angular Damping", &m_angular_damping, 0.0f,  1.0f);
}

void Physics_tool::cancel_ready()
{
    m_state = State::Passive;
}

auto Physics_tool::tool_update() -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!m_mouse_set)
    {
        m_mouse_x = m_pointer_context->mouse_x();
        m_mouse_y = m_pointer_context->mouse_y();
        m_mouse_set = true;
    }
    const double x_delta     = m_pointer_context->mouse_x() - m_mouse_x;
    const double y_delta     = m_pointer_context->mouse_y() - m_mouse_y;
    const bool   mouse_moved = (std::abs(x_delta) > 0.5) || (std::abs(y_delta) > 0.5);
    m_mouse_x = m_pointer_context->mouse_x();
    m_mouse_y = m_pointer_context->mouse_y();

    auto& physics_world = get<Scene_root>()->physics_world();
    if (!physics_world.is_physics_updates_enabled())
    {
        return false;
    }

    m_last_update_frame_number = m_pointer_context->frame_number();

    if (m_pointer_context->priority_action() != Action::drag)
    {
        if (m_state != State::Passive)
        {
            cancel_ready();
        }
        log_tools.trace("Physics tool: Not priority, returning false not consuming\n");
        return false;
    }

    if (
        (m_pointer_context->window() == nullptr) ||
        !m_pointer_context->window()->is_focused() ||
        !m_pointer_context->pointer_in_content_area()
    )
    {
        log_tools.trace("Physics tool: Not view in foucs or not pointer in content area, returning false not consuming\n");
        return false;
    }

    if (m_state == State::Passive)
    {
        if (
            m_pointer_context->hovering_over_content() &&
            m_pointer_context->mouse_button_pressed(Mouse_button_left) &&
            m_pointer_context->position_in_viewport_window().has_value() &&
            m_pointer_context->position_in_world().has_value()
        )
        {
            m_drag_mesh             = m_pointer_context->hover_mesh();
            m_drag_depth            = m_pointer_context->position_in_viewport_window().value().z;
            m_drag_position_in_mesh = m_drag_mesh->transform_point_from_world_to_local(
                m_pointer_context->position_in_world().value()
            );
            m_drag_node_physics     = get_physics_node(m_drag_mesh.get());

            if (
                m_drag_node_physics && 
                (m_drag_node_physics->rigid_body()->get_motion_mode() != erhe::physics::Motion_mode::e_static)
            )
            {
                //log_tools.trace("physics tool drag {} start\n", m_drag_mesh->name());
                m_original_linear_damping  = m_drag_node_physics->rigid_body()->get_linear_damping();
                m_original_angular_damping = m_drag_node_physics->rigid_body()->get_angular_damping();
                m_drag_node_physics->rigid_body()->set_damping(
                    m_linear_damping,
                    m_angular_damping
                );
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
                m_state = State::Ready;

                m_mouse_x = m_pointer_context->mouse_x();
                m_mouse_y = m_pointer_context->mouse_y();

            }
            else
            {
                log_tools.trace("Physics tool drag {} not movable\n", m_drag_mesh->name());
            }
            return true;
        }
        log_tools.trace("Physics tool: Passive - not movable hover object, returning false not consuming\n");
        return false;
    }

    if (m_state == State::Passive)
    {
        log_tools.trace("Physics tool: Passive, returning false not consuming\n");
        return false;
    }

    if (
        (m_state == State::Ready) &&
        mouse_moved
    )
    {
        log_tools.trace("Physics tool drag {} active\n", m_drag_mesh->name());
        m_state = State::Active;
    }

    if (m_state != State::Active)
    {
        log_tools.trace("Physics tool: Not active, returning false not consuming\n");
        return false;
    }

    //log_tools.info("physics_tool::update()\n");
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

    if (m_pointer_context->mouse_button_released(Mouse_button_left))
    {
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
        log_tools.trace("Physics tool drag {} passive\n", m_drag_mesh->name());
        m_drag_mesh.reset();
        m_state = State::Passive;
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
    m_line_renderer->hidden.set_line_color(0xffffffffu);
    m_line_renderer->hidden.add_lines(
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
