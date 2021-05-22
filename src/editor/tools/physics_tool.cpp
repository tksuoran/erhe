#include "tools/physics_tool.hpp"
#include "log.hpp"
#include "renderers/line_renderer.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_manager.hpp"
#include "tools/pointer_context.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/physics/world.hpp"

//#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h>
#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>

#include "imgui.h"

namespace editor
{

using namespace erhe::toolkit;

Physics_tool::Physics_tool()
    : erhe::components::Component{c_name}
{
}

Physics_tool::~Physics_tool() = default;

auto Physics_tool::state() const -> Tool::State
{
    return m_state;
}

void Physics_tool::connect()
{
    m_scene_manager = get<Scene_manager>();
}

void Physics_tool::window(Pointer_context&)
{
    ImGui::Begin("Physics Tool");

    if (m_drag_node_physics)
    {
        auto bullet_angular_factor = m_drag_node_physics->rigid_body.bullet_rigid_body.getAngularFactor();
        float angular_factor[3] = { bullet_angular_factor.x(), bullet_angular_factor.y(), bullet_angular_factor.z() };
        ImGui::InputFloat3("Angular Factor", &angular_factor[0]);
        bullet_angular_factor = btVector3(angular_factor[0], angular_factor[1], angular_factor[2]);
        m_drag_node_physics->rigid_body.bullet_rigid_body.setAngularFactor(bullet_angular_factor);
    }
    ImGui::SliderFloat("Tau",             &m_tau,             0.0f,   1.0f);
    ImGui::SliderFloat("Damping",         &m_damping,         0.0f,   1.0f);
    ImGui::SliderFloat("Impulse Clamp",   &m_impulse_clamp,   0.0f, 100.0f);
    ImGui::SliderFloat("Linear Damping",  &m_linear_damping,  0.0f,   1.0f);
    ImGui::SliderFloat("Angular Damping", &m_angular_damping, 0.0f,   1.0f);
    ImGui::End();
}

void Physics_tool::cancel_ready()
{
    m_state = State::passive;
}

auto Physics_tool::update(Pointer_context& pointer_context) -> bool
{
    if (pointer_context.priority_action != Action::drag)
    {
        if (m_state != State::passive)
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

    if (m_state == State::passive)
    {
        if (pointer_context.hover_content && pointer_context.mouse_button[Mouse_button_left].pressed)
        {
            m_drag_mesh             = pointer_context.hover_mesh;
            m_drag_depth            = pointer_context.pointer_z;
            m_drag_position_in_mesh = glm::vec3(m_drag_mesh->node()->node_from_world() * glm::vec4(pointer_context.position_in_world(), 1.0f));
            m_drag_node_physics     = m_drag_mesh->node()->get_attachment<Node_physics>();
            if (m_drag_node_physics)
            {
                m_original_linear_damping  = m_drag_node_physics->rigid_body.bullet_rigid_body.getLinearDamping();
                m_original_angular_damping = m_drag_node_physics->rigid_body.bullet_rigid_body.getAngularDamping();
                m_drag_node_physics->rigid_body.bullet_rigid_body.setDamping(m_original_linear_damping  + m_linear_damping,
                                                                             m_original_angular_damping + m_angular_damping);
                btVector3 pivot{m_drag_position_in_mesh.x, m_drag_position_in_mesh.y, m_drag_position_in_mesh.z};
                //m_drag_constraint = std::make_unique<btGeneric6DofSpring2Constraint>(node_physics->rigid_body.bullet_rigid_body, pivot, true);
                m_drag_constraint = std::make_unique<btPoint2PointConstraint>(m_drag_node_physics->rigid_body.bullet_rigid_body,
                                                                              pivot);
                m_drag_constraint->m_setting.m_impulseClamp = m_impulse_clamp;
                m_drag_constraint->m_setting.m_damping      = m_damping;
                m_drag_constraint->m_setting.m_tau          = m_tau;
                //m_drag_constraint->setAngularLowerLimit(btVector3(0, 0, 0));
                //m_drag_constraint->setAngularUpperLimit(btVector3(0, 0, 0));
                //
                //for (int i = 0; i < 6; i++) {
                //    m_drag_constraint->setParam(BT_CONSTRAINT_STOP_CFM, m_cfm, i);
                //    m_drag_constraint->setParam(BT_CONSTRAINT_STOP_ERP, m_erp, i);
                //}
                m_drag_node_physics->rigid_body.bullet_rigid_body.setActivationState(DISABLE_DEACTIVATION);
                m_scene_manager->physics_world().bullet_dynamics_world.addConstraint(m_drag_constraint.get());
            }
            log_tools.trace("Physics tool state = ready\n");
            m_state = State::ready;
            return true;
        }
        return false;
    }

    if (m_state == State::passive)
    {
        return false;
    }

    if ((m_state == State::ready) && pointer_context.mouse_moved)
    {
        log_tools.trace("Physics tool state = active\n");
        m_state = State::active;
    }

    if (m_state != State::active)
    {
        return false;
    }

    if (!m_drag_mesh->node())
    {
        return false;
    }

    m_drag_position_start = glm::vec3(m_drag_mesh->node()->world_from_node() * glm::vec4(m_drag_position_in_mesh, 1.0f));
    m_drag_position_end   = pointer_context.position_in_world(m_drag_depth);
    if (m_drag_constraint)
    {
        //btVector3 anchor{m_drag_position_end.x, m_drag_position_end.y, m_drag_position_end.z};
        //
        //m_drag_constraint->setParam(
        m_drag_constraint->setPivotB(btVector3(m_drag_position_end.x, m_drag_position_end.y, m_drag_position_end.z));
    }

    if (pointer_context.mouse_button[Mouse_button_left].released)
    {
        if (m_drag_constraint)
        {
            if (m_drag_node_physics)
            {
                m_drag_node_physics->rigid_body.bullet_rigid_body.setDamping(m_original_linear_damping,
                                                                             m_original_angular_damping);
                m_drag_node_physics->rigid_body.bullet_rigid_body.setActivationState(ACTIVE_TAG);
                m_drag_node_physics.reset();
            }
            m_scene_manager->physics_world().bullet_dynamics_world.removeConstraint(m_drag_constraint.get());
        }
        m_drag_mesh.reset();
        log_tools.trace("Physics tool state = passive\n");
        m_state = State::passive;
    }
    return true;
}


void Physics_tool::render(Render_context& render_context)
{
    render_context.line_renderer->set_line_color(0xffffffffu);
    render_context.line_renderer->add_lines({{m_drag_position_start, m_drag_position_end}}, 4.0f);
    
}

}
