#include "tools/physics_tool.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "renderers/line_renderer.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/physics/iconstraint.hpp"

#include "imgui.h"

namespace editor
{

using namespace erhe::toolkit;

Physics_tool::Physics_tool()
    : erhe::components::Component{c_name}
{
}

Physics_tool::~Physics_tool() = default;

void Physics_tool::connect()
{
    m_scene_root = get<Scene_root>();
}

void Physics_tool::initialize_component()
{
    get<Editor_tools>()->register_tool(this);
}

auto Physics_tool::state() const -> State
{
    return m_state;
}

auto Physics_tool::description() -> const char*
{
    return c_name.data();
}

void Physics_tool::imgui(Pointer_context&)
{
    ImGui::Begin("Physics Tool");

    ImGui::Text("State: %s", Tool::c_state_str[static_cast<int>(m_state)]);
    ImGui::Text("Mesh: %s", m_drag_mesh ? m_drag_mesh->name().c_str() : "");
    ImGui::Text("Drag depth: %f", m_drag_depth);
    ImGui::Text("Constraint: %s", m_drag_constraint ? "yes" : "no");
    ImGui::SliderFloat("Tau",             &m_tau,             0.0f,  0.1f);
    ImGui::SliderFloat("Damping",         &m_damping,         0.0f,  1.0f);
    ImGui::SliderFloat("Impulse Clamp",   &m_impulse_clamp,   0.0f, 10.0f);
    ImGui::SliderFloat("Linear Damping",  &m_linear_damping,  0.0f,  1.0f);
    ImGui::SliderFloat("Angular Damping", &m_angular_damping, 0.0f,  1.0f);
    ImGui::End();
}

void Physics_tool::cancel_ready()
{
    m_state = State::Passive;
}

auto Physics_tool::update(Pointer_context& pointer_context) -> bool
{
    ZoneScoped;

    auto& physics_world = get<Scene_root>()->physics_world();
    if (!physics_world.is_physics_updates_enabled())
    {
        return false;
    }

    m_last_update_frame_number = pointer_context.frame_number;

    if (pointer_context.priority_action != Action::drag)
    {
        if (m_state != State::Passive)
        {
            cancel_ready();
        }
        log_tools.trace("Physics tool: Not priority, returning false not consuming\n");
        return false;
    }

    if (!pointer_context.scene_view_focus ||
        !pointer_context.pointer_in_content_area())
    {
        log_tools.trace("Physics tool: Not view in foucs or not pointer in content area, returning false not consuming\n");
        return false;
    }

    if (m_state == State::Passive)
    {
        if (pointer_context.hover_content && pointer_context.mouse_button[Mouse_button_left].pressed)
        {
            m_drag_mesh             = pointer_context.hover_mesh;
            m_drag_depth            = pointer_context.pointer_z;
            m_drag_position_in_mesh = glm::vec3{m_drag_mesh->node_from_world() * glm::vec4{pointer_context.position_in_world(), 1.0f}};
            m_drag_node_physics     = get_physics_node(m_drag_mesh.get());

            if (m_drag_node_physics && 
                (m_drag_node_physics->rigid_body()->get_motion_mode() != erhe::physics::Motion_mode::e_static))
            {
                //log_tools.trace("physics tool drag {} start\n", m_drag_mesh->name());
                m_original_linear_damping  = m_drag_node_physics->rigid_body()->get_linear_damping();
                m_original_angular_damping = m_drag_node_physics->rigid_body()->get_angular_damping();
                m_drag_node_physics->rigid_body()->set_damping(
                    m_linear_damping,
                    m_angular_damping
                );
                const glm::vec3 pivot{m_drag_position_in_mesh.x, m_drag_position_in_mesh.y, m_drag_position_in_mesh.z};
                m_drag_constraint = erhe::physics::IConstraint::create_point_to_point_constraint_unique(
                    m_drag_node_physics->rigid_body(),
                    pivot
                );
                m_drag_constraint->set_impulse_clamp(m_impulse_clamp);
                m_drag_constraint->set_damping      (m_damping);
                m_drag_constraint->set_tau          (m_tau);
                m_drag_node_physics->rigid_body()->begin_move();
                m_scene_root->physics_world().add_constraint(m_drag_constraint.get());
                log_tools.trace("Physics tool drag {} ready\n", m_drag_mesh->name());
                m_state = State::Ready;
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

    if ((m_state == State::Ready) && pointer_context.mouse_moved)
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

    m_drag_position_start = glm::vec3{m_drag_mesh->world_from_node() * glm::vec4{m_drag_position_in_mesh, 1.0f}};
    m_drag_position_end   = pointer_context.position_in_world(m_drag_depth);

    if (m_drag_constraint)
    {
        m_drag_constraint->set_pivot_in_b(m_drag_position_end);
    }

    if (pointer_context.mouse_button[Mouse_button_left].released)
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

void Physics_tool::update_internal(Pointer_context& pointer_context)
{
}

void Physics_tool::render(const Render_context& render_context)
{
    ZoneScoped;

    if (!m_drag_constraint)
    {
        return;
    }
    render_context.line_renderer->set_line_color(0xffffffffu);
    render_context.line_renderer->add_lines({{m_drag_position_start, m_drag_position_end}}, 4.0f);    
}

}
