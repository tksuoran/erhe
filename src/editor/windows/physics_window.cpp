#include "windows/physics_window.hpp"

#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/debug_draw.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/ext/matrix_common.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Physics_window::Physics_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title, c_type_name}
{
}

Physics_window::~Physics_window() noexcept
{
}

void Physics_window::declare_required_components()
{
    m_editor_scenes = require<Editor_scenes>();

    require<Mesh_memory>();
    require<Programs   >();
    require<Tools      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows>();
}

void Physics_window::initialize_component()
{
    get<Tools>()->register_tool(this);

    //const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};
    //
    //auto rendertarget = get<Editor_imgui_windows>()->create_rendertarget(
    //    "Physics",
    //    1000,
    //    1000,
    //    1000.0
    //);
    //const auto placement = erhe::toolkit::create_look_at(
    //    glm::vec3{-0.5f, 1.0f, 1.0f},
    //    glm::vec3{0.0f, 1.0f, 0.0f},
    //    glm::vec3{0.0f, 1.0f, 0.0f}
    //);
    //rendertarget->mesh_node()->set_parent_from_node(placement);
    //
    //rendertarget->register_imgui_window(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
    m_min_size[0] = 120.0f;
    m_min_size[1] = 120.0f;
}

void Physics_window::post_initialize()
{
    m_selection_tool   = get<Selection_tool  >();
    m_viewport_windows = get<Viewport_windows>();
}

auto Physics_window::description() -> const char*
{
    return c_title.data();
}

void Physics_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    if (m_selection_tool == nullptr)
    {
        return;
    }

    const auto viewport_window = m_viewport_windows->last_window();
    if (!viewport_window)
    {
        return;
    }

    const auto scene_root = viewport_window->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    auto&      physics_world           = scene_root->physics_world();
    const bool physics_enabled         = physics_world.is_physics_updates_enabled();
    bool       updated_physics_enabled = physics_enabled;
    ImGui::Text("Scene: %s", scene_root->name().c_str());
    ImGui::Checkbox("Physics enabled", &updated_physics_enabled);
    if (updated_physics_enabled != physics_enabled)
    {
        if (updated_physics_enabled)
        {
            physics_world.enable_physics_updates();
        }
        else
        {
            physics_world.disable_physics_updates();
        }
    }

    const auto& debug_drawer = get<Debug_draw>();
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

            //const ImVec2 color_button_size{32.0f, 32.0f};
            ImGui::SliderFloat("Line Width", &debug_drawer->line_width, 0.0f, 10.0f);
            ImGui::ColorEdit3("Active",                &m_debug_draw.colors.active_object               .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Deactivated",           &m_debug_draw.colors.deactivated_object          .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Wants Deactivation",    &m_debug_draw.colors.wants_deactivation_object   .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Disabled Deactivation", &m_debug_draw.colors.disabled_deactivation_object.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Disabled Simulation",   &m_debug_draw.colors.disabled_simulation_object  .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("AABB",                  &m_debug_draw.colors.aabb                        .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Contact Point",         &m_debug_draw.colors.contact_point               .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
        }
    }

    const auto gravity = physics_world.get_gravity();
    {
        float floats[3] = { gravity.x, gravity.y, gravity.z };
        ImGui::InputFloat3("Gravity", floats);
        glm::vec3 updated_gravity{ floats[0], floats[1], floats[2] };
        if (updated_gravity != gravity)
        {
            physics_world.set_gravity(updated_gravity);
        }
    }

    const auto& selection = m_selection_tool->selection();
    for (const auto& item : selection)
    {
        auto* node_physics = get_physics_node(item.get()).get();
        if (!node_physics)
        {
            continue;
        }
        ImGui::Text("Rigid body: %s", item->name().c_str());
        auto* rigid_body = node_physics->rigid_body();
        int motion_mode = static_cast<int>(rigid_body->get_motion_mode());

        {
            const glm::mat4 local_inertia = rigid_body->get_local_inertia();
            float floats[4] = { local_inertia[0][0], local_inertia[1][1], local_inertia[2][2] };
            ImGui::InputFloat3("Local Inertia", floats);
            // TODO floats back to rigid body?
        }

        ImGui::Combo(
            "Motion Mode",
            &motion_mode,
            erhe::physics::c_motion_mode_strings,
            IM_ARRAYSIZE(erhe::physics::c_motion_mode_strings)
        );
        rigid_body->set_motion_mode(static_cast<erhe::physics::Motion_mode>(motion_mode));

        float mass = rigid_body->get_mass();
        const float before_mass = mass;
        ImGui::SliderFloat("Mass", &mass, 0.0f, 10.0f);
        if (mass != before_mass)
        {
            glm::mat4 local_inertia{0.0f};
            rigid_body->get_collision_shape()->calculate_local_inertia(mass, local_inertia);
            rigid_body->set_mass_properties(mass, local_inertia);
        }

        float restitution = rigid_body->get_restitution();
        ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f);
        rigid_body->set_restitution(restitution);

        float friction = rigid_body->get_friction();
        ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f);
        rigid_body->set_friction(friction);

        float rolling_friction = rigid_body->get_rolling_friction();
        ImGui::SliderFloat("Rolling Friction", &rolling_friction, 0.0f, 1.0f);
        rigid_body->set_rolling_friction(rolling_friction);

        float linear_damping  = rigid_body->get_linear_damping();
        float angular_damping = rigid_body->get_angular_damping();
        ImGui::SliderFloat("Linear Damping",  &linear_damping, 0.0f, 1.0f);
        ImGui::SliderFloat("Angular Damping", &angular_damping, 0.0f, 1.0f);
        rigid_body->set_damping(linear_damping, angular_damping);
    }
#endif
}

auto Physics_window::get_debug_draw_parameters() -> Debug_draw_parameters
{
    return m_debug_draw;
}

void Physics_window::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    if (context.scene_view == nullptr)
    {
        return;
    }
    const auto& scene_root   = context.scene_view->get_scene_root();
    const auto& debug_drawer = get<Debug_draw>();
    if (
        !debug_drawer ||
        !m_debug_draw.enable ||
        !scene_root
    )
    {
        return;
    }

    int flags = 0;
    if (m_debug_draw.wireframe        ) flags |= erhe::physics::IDebug_draw::c_Draw_wireframe;
    if (m_debug_draw.aabb             ) flags |= erhe::physics::IDebug_draw::c_Draw_aabb;
    if (m_debug_draw.contact_points   ) flags |= erhe::physics::IDebug_draw::c_Draw_contact_points;
    if (m_debug_draw.no_deactivation  ) flags |= erhe::physics::IDebug_draw::c_No_deactivation;
    if (m_debug_draw.constraints      ) flags |= erhe::physics::IDebug_draw::c_Draw_constraints;
    if (m_debug_draw.constraint_limits) flags |= erhe::physics::IDebug_draw::c_Draw_constraint_limits;
    if (m_debug_draw.normals          ) flags |= erhe::physics::IDebug_draw::c_Draw_normals;
    if (m_debug_draw.frames           ) flags |= erhe::physics::IDebug_draw::c_Draw_frames;
    debug_drawer->set_debug_mode(flags);

    debug_drawer->set_colors(m_debug_draw.colors);

    scene_root->physics_world().debug_draw();
}

} // namespace editor
