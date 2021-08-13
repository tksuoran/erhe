#include "windows/physics_window.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "scene/debug_draw.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/pointer_context.hpp"

#include "erhe/physics/world.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

namespace editor
{

Physics_window::Physics_window()
    : erhe::components::Component(c_name)
{
}

Physics_window::~Physics_window() = default;

void Physics_window::connect()
{
    m_selection_tool  = get    <Selection_tool>();
    m_scene_root      = require<Scene_root    >();
}

void Physics_window::initialize_component()
{
    get<Editor_tools>()->register_tool(this);
}

auto Physics_window::state() const -> State
{
    return State::Passive;
}

auto Physics_window::description() -> const char*
{
    return c_name;
}

auto to_bullet(glm::vec3 glm_vec3) -> btVector3
{
    return btVector3{btScalar{glm_vec3.x}, btScalar{glm_vec3.y}, btScalar{glm_vec3.z}};
}

void Physics_window::window(Pointer_context& pointer_context)
{
    if (m_selection_tool.get() == nullptr)
    {
        return;
    }

    ImGui::Begin("Physics");

    const auto button_size = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);
    auto& physics_world = m_scene_root->physics_world();
    ImGui::Checkbox("Physics enabled", &physics_world.physics_enabled);

    auto debug_drawer = get<Debug_draw>();
    if (debug_drawer)
    {
        if (ImGui::CollapsingHeader("Visualizations"))
        {
            ImGui::Checkbox("Enabled", &m_debug_draw.enable);
            if (m_debug_draw.enable)
            {
                ImGui::Checkbox("Wireframe",         &m_debug_draw.wireframe        );
                ImGui::Checkbox("AABB",              &m_debug_draw.aabb             );
                ImGui::Checkbox("Context Points",    &m_debug_draw.contact_points   );
                ImGui::Checkbox("No Deactivation",   &m_debug_draw.no_deactivation  );
                ImGui::Checkbox("Constraints",       &m_debug_draw.constraints      );
                ImGui::Checkbox("Constraint Limits", &m_debug_draw.constraint_limits);
                ImGui::Checkbox("Normals",           &m_debug_draw.normals          );
                ImGui::Checkbox("Frames",            &m_debug_draw.frames           );
            }
            
            const ImVec2 color_button_size{32.0f, 32.0f};
            ImGui::SliderFloat("Line Width", &debug_drawer->line_width, 0.0f, 10.0f);
            ImGui::ColorEdit3("Active",                &m_debug_draw.default_colors.active_object               .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Deactivated",           &m_debug_draw.default_colors.deactivated_object          .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Wants Deactivation",    &m_debug_draw.default_colors.wants_deactivation_object   .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Disabled Deactivation", &m_debug_draw.default_colors.disabled_deactivation_object.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Disabled Simulation",   &m_debug_draw.default_colors.disabled_simulation_object  .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("AABB",                  &m_debug_draw.default_colors.aabb                        .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Contact Point",         &m_debug_draw.default_colors.contact_point               .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
        }
    }

    const auto gravity = physics_world.bullet_dynamics_world.getGravity();
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

    const auto& selecion = m_selection_tool->selection();
    for (const auto item : selecion)
    {
        const auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
        if (!mesh)
        {
            continue;
        }

        const auto* node = mesh->node().get();
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
            const btVector3 local_inertia = rigid_body.bullet_rigid_body.getLocalInertia();
            float floats[3] = { local_inertia.x(), local_inertia.y(), local_inertia.z() };
            ImGui::InputFloat3("Local Inertia", floats);
            // TODO floats back to rigid body?
        }

        ImGui::Combo("Collision Mode",
                     &collision_mode,
                     erhe::physics::Rigid_body::c_collision_mode_strings,
                     IM_ARRAYSIZE(erhe::physics::Rigid_body::c_collision_mode_strings));
        rigid_body.set_collision_mode(static_cast<erhe::physics::Rigid_body::Collision_mode>(collision_mode));

        float mass = rigid_body.bullet_rigid_body.getMass();
        const float before_mass = mass;
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
    }
    ImGui::End();
}

auto Physics_window::get_debug_draw_parameters() -> Debug_draw_parameters
{
    return m_debug_draw;
}

void Physics_window::render(const Render_context& render_context)
{
    ZoneScoped;

    auto debug_drawer = get<Debug_draw>();
    if (!debug_drawer || !m_debug_draw.enable || !m_scene_root)
    {
        return;
    }

    int flags = 0;
    if (m_debug_draw.wireframe        ) flags |= btIDebugDraw::DebugDrawModes::DBG_DrawWireframe;
    if (m_debug_draw.aabb             ) flags |= btIDebugDraw::DebugDrawModes::DBG_DrawAabb;
    if (m_debug_draw.contact_points   ) flags |= btIDebugDraw::DebugDrawModes::DBG_DrawContactPoints;
    if (m_debug_draw.no_deactivation  ) flags |= btIDebugDraw::DebugDrawModes::DBG_NoDeactivation;
    if (m_debug_draw.constraints      ) flags |= btIDebugDraw::DebugDrawModes::DBG_DrawConstraints;
    if (m_debug_draw.constraint_limits) flags |= btIDebugDraw::DebugDrawModes::DBG_DrawConstraintLimits;
    if (m_debug_draw.normals          ) flags |= btIDebugDraw::DebugDrawModes::DBG_DrawNormals;
    if (m_debug_draw.frames           ) flags |= btIDebugDraw::DebugDrawModes::DBG_DrawFrames;
    debug_drawer->setDebugMode(flags);

    btIDebugDraw::DefaultColors bullet_default_colors;
	bullet_default_colors.m_activeObject               = to_bullet(m_debug_draw.default_colors.active_object               );
	bullet_default_colors.m_deactivatedObject          = to_bullet(m_debug_draw.default_colors.deactivated_object          );
	bullet_default_colors.m_wantsDeactivationObject    = to_bullet(m_debug_draw.default_colors.wants_deactivation_object   );
	bullet_default_colors.m_disabledDeactivationObject = to_bullet(m_debug_draw.default_colors.disabled_deactivation_object);
	bullet_default_colors.m_disabledSimulationObject   = to_bullet(m_debug_draw.default_colors.disabled_simulation_object  );
	bullet_default_colors.m_aabb                       = to_bullet(m_debug_draw.default_colors.aabb                        );
	bullet_default_colors.m_contactPoint               = to_bullet(m_debug_draw.default_colors.contact_point               );
    debug_drawer->setDefaultColors(bullet_default_colors);

    m_scene_root->physics_world().bullet_dynamics_world.debugDrawWorld();
}

} // namespace editor
