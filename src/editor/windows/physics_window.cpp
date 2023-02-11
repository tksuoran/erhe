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

#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/ext/matrix_common.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Physics_window* g_physics_window{nullptr};

Physics_window::Physics_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Physics_window::~Physics_window() noexcept
{
    ERHE_VERIFY(g_physics_window == this);
    g_physics_window = nullptr;
}

void Physics_window::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows>();
    require<Editor_scenes>();
}

void Physics_window::initialize_component()
{
    ERHE_VERIFY(g_physics_window == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this, "physics");
    m_min_size[0] = 120.0f;
    m_min_size[1] = 120.0f;
    g_physics_window = this;
}

void Physics_window::viewport_toolbar(bool& hovered)
{
    const auto viewport_window = g_viewport_windows->last_window();
    if (!viewport_window)
    {
        return;
    }

    const auto scene_root = viewport_window->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    auto& physics_world   = scene_root->physics_world();
    bool  physics_enabled = physics_world.is_physics_updates_enabled();

    ImGui::SameLine();
    const bool pressed = erhe::application::make_button(
        "P",
        (physics_enabled)
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal
    );
    if (ImGui::IsItemHovered())
    {
        hovered = true;
        ImGui::SetTooltip(
            physics_enabled
                ? "Toggle physics on -> off"
                : "Toggle physics off -> on"
        );
    };

    if (pressed)
    {
        if (physics_enabled)
        {
            physics_world.disable_physics_updates();
        }
        else
        {
            physics_world.enable_physics_updates();
        }
    }
}

void Physics_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    if (g_selection_tool == nullptr)
    {
        return;
    }

    const auto viewport_window = g_viewport_windows->last_window();
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
    ImGui::Text("Scene: %s", scene_root->get_name().c_str());
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

    if (g_debug_draw != nullptr)
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
            ImGui::SliderFloat("Line Width", &g_debug_draw->line_width, 0.0f, 10.0f);
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
    if (
        (g_debug_draw == nullptr) ||
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
    g_debug_draw->set_debug_mode(flags);

    g_debug_draw->set_colors(m_debug_draw.colors);

    scene_root->physics_world().debug_draw();
}

} // namespace editor
