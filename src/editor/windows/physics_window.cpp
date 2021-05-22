#include "windows/physics_window.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_manager.hpp"
#include "tools/selection_tool.hpp"
#include "tools/pointer_context.hpp"
#include "log.hpp"
#include "erhe/physics/world.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

namespace editor
{

auto Physics_window::state() const -> Tool::State
{
    return State::passive;
}

void Physics_window::connect()
{
    m_selection_tool  = get    <Selection_tool>();
    m_scene_manager   = require<Scene_manager>();
}

void Physics_window::window(Pointer_context& pointer_context)
{
    if (m_selection_tool.get() == nullptr)
    {
        return;
    }

    ImGui::Begin("Physics");

    auto button_size = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);
    auto& physics_world = m_scene_manager->physics_world();
    ImGui::Checkbox("Physics enabled", &physics_world.physics_enabled);
    ImGui::Checkbox("Debug draw", &m_debug_draw);

    auto gravity = physics_world.bullet_dynamics_world.getGravity();
    {
        float floats[3] = { gravity.x(), gravity.y(), gravity.z() };
        ImGui::InputFloat3("Gravity", floats);
        auto updated_gravity = btVector3{btScalar{floats[0]},
                                         btScalar{floats[1]},
                                         btScalar{floats[2]}};
        if (updated_gravity != gravity)
        {
            physics_world.bullet_dynamics_world.setGravity(updated_gravity);
        }
    }

    const auto& selected_meshes = m_selection_tool->selected_meshes();
    for (const auto& mesh : selected_meshes)
    {
        if (!mesh)
        {
            continue;
        }
        auto* node = mesh->node().get();
        if (node == nullptr)
        {
            continue;
        }
        auto node_physics = node->get_attachment<Node_physics>();
        if (!node_physics)
        {
            continue;
        }
        ImGui::Text("Rigid body: %s", mesh->name().c_str());
        auto& rigid_body = node_physics->rigid_body;
        int collision_mode = static_cast<int>(rigid_body.get_collision_mode());

        {
            btVector3 local_inertia = rigid_body.bullet_rigid_body.getLocalInertia();
            float floats[3] = { local_inertia.x(), local_inertia.y(), local_inertia.z() };
            ImGui::InputFloat3("Local Inertia", floats);
        }

        ImGui::Combo("Collision Mode",
                     &collision_mode,
                     erhe::physics::Rigid_body::c_collision_mode_strings,
                     IM_ARRAYSIZE(erhe::physics::Rigid_body::c_collision_mode_strings));
        rigid_body.set_collision_mode(static_cast<erhe::physics::Rigid_body::Collision_mode>(collision_mode));

        float mass = rigid_body.bullet_rigid_body.getMass();
        float before_mass = mass;
        ImGui::SliderFloat("Mass", &mass, 0.0f, 10.0f);
        if (mass != before_mass)
        {
            btVector3 local_inertia;
            rigid_body.bullet_rigid_body.getCollisionShape()->calculateLocalInertia(mass, local_inertia);
            rigid_body.bullet_rigid_body.setMassProps(mass, local_inertia);
        }

        float restitution = rigid_body.get_restitution();
        ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f);
        rigid_body.set_restitution(restitution);

        float friction = rigid_body.get_friction();
        ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f);
        rigid_body.set_friction(friction);

        float rolling_friction = rigid_body.bullet_rigid_body.getRollingFriction();
        ImGui::SliderFloat("Rolling Friction", &rolling_friction, 0.0f, 1.0f);
        rigid_body.bullet_rigid_body.setRollingFriction(rolling_friction);

        float linear_damping  = rigid_body.bullet_rigid_body.getLinearDamping();
        float angular_damping = rigid_body.bullet_rigid_body.getAngularDamping();
        ImGui::SliderFloat("Linear Damping",  &linear_damping, 0.0f, 1.0f);
        ImGui::SliderFloat("Angular Damping", &angular_damping, 0.0f, 1.0f);
        rigid_body.bullet_rigid_body.setDamping(linear_damping, angular_damping);

        //auto bullet_angular_factor = rigid_body.bullet_rigid_body.getAngularFactor();
        //float angular_factor[3] = { bullet_angular_factor.x(), bullet_angular_factor.y(), bullet_angular_factor.z() };
        //ImGui::InputFloat3("Angular Factor", &angular_factor[0]);
        //bullet_angular_factor = btVector3(angular_factor[0], angular_factor[1], angular_factor[2]);
        //rigid_body.bullet_rigid_body.setAngularFactor(bullet_angular_factor);
    }
    ImGui::End();
}

void Physics_window::render(Render_context& render_context)
{
    if (m_debug_draw && m_scene_manager)
    {
        m_scene_manager->debug_render();
    }
}

} // namespace editor
