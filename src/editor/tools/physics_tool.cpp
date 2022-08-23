#include "tools/physics_tool.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/tools.hpp"
#include "windows/operations.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/physics/iconstraint.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

void Physics_tool_drag_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (m_physics_tool.on_drag_ready())
    {
        set_ready(context);
    }
}

auto Physics_tool_drag_command::try_call(
    erhe::application::Command_context&
context) -> bool
{
    if (state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        m_physics_tool.on_drag() &&
        (state() == erhe::application::State::Ready)
    )
    {
        set_active(context);
    }

    return state() == erhe::application::State::Active;
}

void Physics_tool_drag_command::on_inactive(
    erhe::application::Command_context& context
)
{
    static_cast<void>(context);

    log_physics->info("Physics_tool_drag_command::on_inactive() - command state = {}", c_str(state()));
    if (state() == erhe::application::State::Active)
    {
        m_physics_tool.release_target();
    }
}

////////

void Physics_tool_force_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (m_physics_tool.on_force_ready())
    {
        //set_ready(context);
        set_active(context);
    }
}

auto Physics_tool_force_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (state() == erhe::application::State::Inactive)
    {
        return false;
    }

    if (
        m_physics_tool.on_force() &&
        (state() == erhe::application::State::Ready)
    )
    {
        set_active(context);
    }

    return state() == erhe::application::State::Active;
}

void Physics_tool_force_command::on_inactive(
    erhe::application::Command_context& context
)
{
    static_cast<void>(context);

    log_physics->info("Physics_tool_force_command::on_inactive() - command state = {}", c_str(state()));

    if (state() == erhe::application::State::Active)
    {
        m_physics_tool.release_target();
    }
}

/////////

Physics_tool::Physics_tool()
    : erhe::components::Component{c_type_name}
    , m_drag_command             {*this}
    , m_force_command            {*this}
{
}

Physics_tool::~Physics_tool() noexcept
{
    if (m_target_constraint)
    {
        auto* world = physics_world();
        if (world != nullptr)
        {
            world->remove_constraint(m_target_constraint.get());
        }
    }
}

[[nodiscard]] auto Physics_tool::physics_world() const -> erhe::physics::IWorld*
{
    ERHE_VERIFY(m_editor_scenes);

    if (!m_target_mesh)
    {
        return nullptr;
    }

    auto* scene_root = reinterpret_cast<Scene_root*>(m_target_mesh->node_data.host);
    if (scene_root == nullptr)
    {
        return nullptr;
    }

    return &scene_root->physics_world();
}

void Physics_tool::declare_required_components()
{
    require<Tools                  >();
    require<erhe::application::View>();
    require<Operations             >();
}

void Physics_tool::initialize_component()
{
    get<Tools>()->register_tool(this);

    const auto view = get<erhe::application::View>();
    view->register_command(&m_drag_command);
    view->register_command(&m_force_command);
    view->bind_command_to_mouse_drag(&m_drag_command, erhe::toolkit::Mouse_button_right);
    view->bind_command_to_mouse_drag(&m_force_command, erhe::toolkit::Mouse_button_right);

    erhe::application::Command_context context{*view.get()};
    set_active_command(c_command_drag);

    get<Operations>()->register_active_tool(this);
}

void Physics_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_editor_scenes     = get<Editor_scenes>();
    m_fly_camera        = get<Fly_camera_tool>();
    m_viewport_windows  = get<Viewport_windows>();
}

auto Physics_tool::description() -> const char*
{
    return c_title.data();
}

auto Physics_tool::acquire_target() -> bool
{
    Viewport_window* viewport_window = m_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return false;
    }

    const auto& content = viewport_window->get_hover(Hover_entry::content_slot);
    if (
        !content.valid ||
        !content.position.has_value()
    )
    {
        return false;
    }

    const auto p0_opt = viewport_window->near_position_in_world();
    if (!p0_opt.has_value())
    {
        return false;
    }

    const glm::vec3 view_position   = p0_opt.value();
    const glm::vec3 target_position = content.position.value();

    m_target_mesh             = content.mesh;
    m_target_distance         = glm::distance(view_position, target_position);
    m_target_position_in_mesh = m_target_mesh->transform_point_from_world_to_local(
        content.position.value()
    );
    m_target_node_physics     = get_physics_node(m_target_mesh.get());

    if (!m_target_node_physics)
    {
        return false;
    }

    erhe::physics::IRigid_body* rigid_body = m_target_node_physics->rigid_body();
    if (rigid_body->get_motion_mode() == erhe::physics::Motion_mode::e_static)
    {
        return false;
    }

    m_original_linear_damping  = rigid_body->get_linear_damping();
    m_original_angular_damping = rigid_body->get_angular_damping();

    rigid_body->set_damping(
        m_linear_damping,
        m_angular_damping
    );

    // TODO Make this happen automatically
    if (m_target_constraint)
    {
        auto* world = physics_world();
        if (world != nullptr)
        {
            world->remove_constraint(m_target_constraint.get());
        }
        /// TODO Should we also do this? m_target_constraint.reset();
    }

    log_physics->info("Physics_tool: Target acquired");

    return true;
}

void Physics_tool::release_target()
{
    log_physics->info("Physics_tool: Target released");

    if (m_target_constraint)
    {
        if (m_target_node_physics)
        {
            erhe::physics::IRigid_body* rigid_body = m_target_node_physics->rigid_body();
            rigid_body->set_damping(
                m_original_linear_damping,
                m_original_angular_damping
            );
            rigid_body->end_move();
            m_target_node_physics.reset();
        }
        auto* world = physics_world();
        if (world != nullptr)
        {
            world->remove_constraint(m_target_constraint.get());
        }
        m_target_constraint.reset();
    }
    m_target_mesh.reset();
}

void Physics_tool::begin_point_to_point_constraint()
{
    log_physics->info("Physics_tool: Begin point to point constraint");

    m_target_constraint = erhe::physics::IConstraint::create_point_to_point_constraint_unique(
        m_target_node_physics->rigid_body(),
        m_target_position_in_mesh
    );
    m_target_constraint->set_impulse_clamp(m_impulse_clamp);
    m_target_constraint->set_damping      (m_damping);
    m_target_constraint->set_tau          (m_tau);
    m_target_node_physics->rigid_body()->begin_move();
    auto* world = physics_world();
    if (world != nullptr)
    {
        world->add_constraint(m_target_constraint.get());
    }
}

auto Physics_tool::on_drag_ready() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    if (!acquire_target())
    {
        log_physics->info("Physics tool drag - acquire target failed");
        return false;
    }

    begin_point_to_point_constraint();

    log_physics->info("Physics tool drag {} ready", m_target_mesh->name());
    return true;
}

auto Physics_tool::on_force_ready() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    if (!acquire_target())
    {
        log_physics->info("Physics tool force - acquire target failed");
        return false;
    }

    begin_point_to_point_constraint();

    log_physics->info("Physics tool force {} ready", m_target_mesh->name());
    return true;
}

auto Physics_tool::on_drag() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    auto* world = physics_world();
    if (world == nullptr)
    {
        return false;
    }
    if (!world->is_physics_updates_enabled())
    {
        return false;
    }

    Viewport_window* viewport_window = m_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return false;
    }

    const auto end = viewport_window->position_in_world_distance(m_target_distance);
    if (!end.has_value())
    {
        return false;
    }

    if (!m_target_mesh)
    {
        return false;
    }

    m_target_position_start = glm::vec3{m_target_mesh->world_from_node() * glm::vec4{m_target_position_in_mesh, 1.0f}};
    m_target_position_end   = end.value();

    if (m_target_constraint)
    {
        m_target_constraint->set_pivot_in_b(m_target_position_end);
    }

    return true;
}

auto Physics_tool::on_force() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    auto* world = physics_world();
    if (world == nullptr)
    {
        return false;
    }
    if (!world->is_physics_updates_enabled())
    {
        return false;
    }

    Viewport_window* viewport_window = m_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return false;
    }

    const auto end = viewport_window->near_position_in_world();
    if (!end.has_value())
    {
        return false;
    }

    if (!m_target_mesh)
    {
        return false;
    }

    if (!m_target_constraint)
    {
        return false;
    }

    m_target_position_start = glm::vec3{m_target_mesh->world_from_node() * glm::vec4{m_target_position_in_mesh, 1.0f}};

    float max_radius = 0.0f;
    for (const auto& primitive : m_target_mesh->mesh_data.primitives)
    {
        max_radius = std::max(
            max_radius,
            primitive.gl_primitive_geometry.bounding_sphere.radius
        );
    }
    m_target_mesh_size   = max_radius;
    m_to_end_direction   = glm::normalize(end.value() - m_target_position_start);
    m_to_start_direction = glm::normalize(m_target_position_start - end.value());
    const float distance = glm::distance(end.value(), m_target_position_start);
    if (distance > max_radius * 4.0f)
    {
        m_target_position_end = m_target_position_start + m_force_distance * distance * m_to_end_direction;
    }
    else
    {
        m_target_position_end = end.value() + m_force_distance * distance * m_to_start_direction;
    }
    m_target_distance = distance;

    m_target_constraint->set_pivot_in_b(m_target_position_end);

    return true;
}

void Physics_tool::update_fixed_step(const erhe::components::Time_context&)
{
    if (m_force_command.state() == erhe::application::State::Active)
    {
        on_force();
    }
}

void Physics_tool::tool_render(const Render_context& /*context*/)
{
    ERHE_PROFILE_FUNCTION

    if (!m_target_constraint)
    {
        return;
    }
    erhe::application::Line_renderer& line_renderer = m_line_renderer_set->hidden;
    line_renderer.set_line_color(0xffffffffu);
    line_renderer.set_thickness(4.0f);
    line_renderer.add_lines(
        {
            {
                m_target_position_start,
                m_target_position_end
            }
        }
    );
    m_line_renderer_set->hidden.set_line_color(0xffff0000u);
    m_line_renderer_set->hidden.add_lines(
        {
            {
                m_target_position_start,
                m_target_position_start + m_to_end_direction
            }
        }
    );
    m_line_renderer_set->hidden.set_line_color(0xff00ff00u);
    m_line_renderer_set->hidden.add_lines(
        {
            {
                m_target_position_start,
                m_target_position_start + m_to_start_direction
            }
        }
    );
}

void Physics_tool::set_active_command(const int command)
{
    m_active_command = command;
    erhe::application::Command_context context{
        *get<erhe::application::View>().get()
    };

    switch (command)
    {
        case c_command_drag:
        {
            m_drag_command.enable(context);
            m_force_command.disable(context);
            break;
        }
        case c_command_force:
        {
            m_drag_command.disable(context);
            m_force_command.enable(context);
            break;
        }
        default:
        {
            break;
        }
    }
}

void Physics_tool::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    int command = m_active_command;
    ImGui::RadioButton("Drag",  &command, c_command_drag); ImGui::SameLine();
    ImGui::RadioButton("Force", &command, c_command_force);
    if (command != m_active_command)
    {
        set_active_command(command);
    }

    ImGui::Text("Drag state: %s",  erhe::application::c_state_str[static_cast<int>(m_drag_command.state())]);
    ImGui::Text("Force state: %s", erhe::application::c_state_str[static_cast<int>(m_force_command.state())]);
    ImGui::Text("Mesh: %s",            m_target_mesh ? m_target_mesh->name().c_str() : "");
    ImGui::Text("Drag distance: %f",   m_target_distance);
    ImGui::Text("Target distance: %f", m_target_distance);
    ImGui::Text("Target Size: %f",     m_target_mesh_size);
    ImGui::Text("Constraint: %s",      m_target_constraint ? "yes" : "no");
    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
    ImGui::SliderFloat("Force Distance",  &m_force_distance, -10.0f, 10.0f, "%.2f", logarithmic);
    ImGui::SliderFloat("Tau",             &m_tau,             0.0f,   0.1f);
    ImGui::SliderFloat("Damping",         &m_damping,         0.0f,   1.0f);
    ImGui::SliderFloat("Impulse Clamp",   &m_impulse_clamp,   0.0f, 100.0f);
    ImGui::SliderFloat("Linear Damping",  &m_linear_damping,  0.0f,   1.0f);
    ImGui::SliderFloat("Angular Damping", &m_angular_damping, 0.0f,   1.0f);
#endif
}

} // namespace editor
