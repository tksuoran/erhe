#include "tools/physics_tool.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/tools.hpp"
#include "windows/operations.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/view.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/iconstraint.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

class Scene_builder;

void Physics_tool_drag_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (get_tool_state() != erhe::application::State::Inactive)
    {
        log_physics->trace("PT state not inactive");
        return;
    }

    Scene_view* scene_view = reinterpret_cast<Scene_view*>(context.get_input_context());
    if (m_physics_tool.on_drag_ready(scene_view))
    {
        log_physics->trace("PT set ready");
        set_ready(context);
    }
}

auto Physics_tool_drag_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (get_tool_state() == erhe::application::State::Inactive)
    {
        return false;
    }

    Scene_view* scene_view = reinterpret_cast<Scene_view*>(context.get_input_context());
    if (
        m_physics_tool.on_drag(scene_view) &&
        (get_tool_state() == erhe::application::State::Ready)
    )
    {
        set_active(context);
    }

    return get_tool_state() == erhe::application::State::Active;
}

void Physics_tool_drag_command::on_inactive(
    erhe::application::Command_context& context
)
{
    static_cast<void>(context);

    log_physics->trace("PT on_inactive");
    if (
        (get_tool_state() == erhe::application::State::Ready ) ||
        (get_tool_state() == erhe::application::State::Active)
    )
    {
        m_physics_tool.release_target();
    }
}

Physics_tool::Physics_tool()
    : erhe::components::Component{c_type_name}
    , m_drag_command             {*this}
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
        m_target_constraint.reset();
    }
}

[[nodiscard]] auto Physics_tool::physics_world() const -> erhe::physics::IWorld*
{
    ERHE_VERIFY(m_editor_scenes);

    if (!m_target_mesh)
    {
        const auto scene_builder = get<Scene_builder>();
        if (scene_builder)
        {
            const auto scene_root = scene_builder->get_scene_root();
            if (scene_root)
            {
                auto& world = scene_root->physics_world();
                return &world;
            }
        }

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
    require<erhe::application::Commands>();
    require<Operations   >();
    require<Tools        >();
    require<Scene_builder>();
    m_editor_scenes     = require<Editor_scenes>();
}

void Physics_tool::initialize_component()
{
    const auto& tools = get<Tools>();
    tools->register_tool(this);
    tools->register_hover_tool(this);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_drag_command);
    commands->bind_command_to_mouse_drag(&m_drag_command, erhe::toolkit::Mouse_button_right);
    commands->bind_command_to_controller_trigger(&m_drag_command, 0.5f, 0.45f, true);
    get<Operations>()->register_active_tool(this);

    erhe::physics::IWorld* world = physics_world();
    if (world == nullptr)
    {
        log_physics->error("No physics world");
        return;
    }

    // TODO store world and use stored world for these removes.
    if (m_target_constraint)
    {
        world->remove_constraint(m_target_constraint.get());
        m_target_constraint.reset();
    }

    m_constraint_world_point_rigid_body = erhe::physics::IRigid_body::create_rigid_body_shared(
        erhe::physics::IRigid_body_create_info
        {
            .world             = *world,
            .mass              = 10.0f,
            .collision_shape   = erhe::physics::ICollision_shape::create_empty_shape_shared(),
            .debug_label       = "Physics Tool",
            .enable_collisions = false
        },
        this
    );
    world->add_rigid_body(m_constraint_world_point_rigid_body.get());

}

void Physics_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_fly_camera        = get<Fly_camera_tool>();
    m_viewport_windows  = get<Viewport_windows>();
}

auto Physics_tool::description() -> const char*
{
    return c_title.data();
}

[[nodiscard]] auto Physics_tool::get_mode() const -> Physics_tool_mode
{
    return m_mode;
}

void Physics_tool::set_mode(Physics_tool_mode value)
{
    m_mode = value;
}

[[nodiscard]] auto Physics_tool::get_world_from_rigidbody() const -> erhe::physics::Transform
{
    return erhe::physics::Transform{
        glm::mat3{1.0f},
        m_target_position_end
    };
}

[[nodiscard]] auto Physics_tool::get_motion_mode() const -> erhe::physics::Motion_mode
{
    return m_motion_mode;
}

void Physics_tool::set_world_from_rigidbody(const erhe::physics::Transform transform)
{
    log_physics->warn("Physics_tool::set_world_from_rigidbody() - This should not happen");
    static_cast<void>(transform);
}

void Physics_tool::set_motion_mode(const erhe::physics::Motion_mode motion_mode)
{
    log_physics->warn("Physics_tool::set_motion_mode() - This should not happen?");
    static_cast<void>(motion_mode);
}

auto Physics_tool::acquire_target(Scene_view* scene_view) -> bool
{
    log_physics->warn("acquire_target() ...");
    if (scene_view == nullptr)
    {
        log_physics->warn("Cant target: No scene_view");
        return false;
    }

    const auto& content = scene_view->get_hover(Hover_entry::content_slot);
    if (
        !content.valid ||
        !content.position.has_value()
    )
    {
        log_physics->warn("Cant target: No content");
        return false;
    }

    const auto p0_opt = scene_view->get_control_ray_origin_in_world();
    if (!p0_opt.has_value())
    {
        log_physics->warn("Cant target: No ray origin");
        return false;
    }

    const glm::vec3 view_position   = p0_opt.value();
    const glm::vec3 target_position = glm::mix(
        glm::vec3{content.position.value()},
        glm::vec3{content.mesh->position_in_world()},
        m_depth
    );

    m_target_mesh     = content.mesh;
    m_target_distance = glm::distance(view_position, target_position);
    m_target_position_in_mesh = m_target_mesh->transform_point_from_world_to_local(
        target_position
    );
    m_target_node_physics     = get_physics_node(m_target_mesh.get());

    if (!m_target_node_physics)
    {
        log_physics->warn("Cant target: No physics mesh");
        m_target_mesh.reset();
        return false;
    }

    erhe::physics::IRigid_body* rigid_body = m_target_node_physics->rigid_body();
    if (rigid_body->get_motion_mode() == erhe::physics::Motion_mode::e_static)
    {
        log_physics->warn("Cant target: Static mesh");
        return false;
    }

    m_original_linear_damping  = rigid_body->get_linear_damping();
    m_original_angular_damping = rigid_body->get_angular_damping();
    m_original_friction        = rigid_body->get_friction();
    m_original_gravity         = rigid_body->get_gravity_factor();

    rigid_body->set_damping(
        m_override_linear_damping,
        m_override_angular_damping
    );
    rigid_body->set_friction(m_override_friction);
    rigid_body->set_gravity_factor(m_override_gravity);

    // TODO Make this happen automatically
    if (m_target_constraint)
    {
        auto* world = physics_world();
        if (world != nullptr)
        {
            world->remove_constraint(m_target_constraint.get());
        }
        m_target_constraint.reset();
    }

    log_physics->trace("PT Target acquired");

    return true;
}

void Physics_tool::release_target()
{
    log_physics->trace("PT Target released");

    if (m_target_constraint)
    {
        physics_world()->remove_constraint(m_target_constraint.get());
        m_target_constraint.reset();
    }

    if (m_target_node_physics)
    {
        erhe::physics::IRigid_body* rigid_body = m_target_node_physics->rigid_body();
        const glm::vec3 end_velocity = rigid_body->get_linear_velocity();
        rigid_body->set_damping(
            m_original_linear_damping,
            m_original_angular_damping
        );
        rigid_body->set_friction(m_original_friction);
        rigid_body->set_gravity_factor(m_original_gravity);
        rigid_body->end_move();
        m_target_node_physics.reset();
    }

    m_target_mesh.reset();

    m_target_distance         = 1.0;
    m_target_position_in_mesh = glm::dvec3{0.0, 0.0, 0.0};
    m_target_position_end     = glm::dvec3{0.0, 0.0, 0.0};
    m_to_end_direction        = glm::dvec3{0.0};
    m_to_start_direction      = glm::dvec3{0.0};
    m_target_mesh_size        = 0.0;

}

void Physics_tool::begin_point_to_point_constraint()
{
    log_physics->trace("PT Begin point to point constraint");

    erhe::physics::IWorld* world = physics_world();
    if (world == nullptr)
    {
        log_physics->error("No physics world");
        return;
    }

    // TODO store world and use stored world for these removes?
    if (m_target_constraint)
    {
        world->remove_constraint(m_target_constraint.get());
        m_target_constraint.reset();
    }

    m_target_position_end = glm::dvec3{
        m_target_mesh->world_from_node() * glm::vec4{m_target_position_in_mesh, 1.0f}
    };

    log_physics->trace("PT pos in mesh {}", m_target_position_in_mesh);
    log_physics->trace("PT drag {} ready @ {}", m_target_mesh->name(), m_target_position_end);

    m_constraint_world_point_rigid_body->set_world_transform(
        erhe::physics::Transform{
            glm::mat3{1.0f},
            glm::vec3{m_target_position_end}
        }
    );

    const erhe::physics::Point_to_point_constraint_settings constraint_settings
    {
        .rigid_body_a = m_target_node_physics->rigid_body(),
        .rigid_body_b = m_constraint_world_point_rigid_body.get(),
        .pivot_in_a   = m_target_position_in_mesh,
        .pivot_in_b   = glm::vec3{0.0f, 0.0f, 0.0f},
        .frequency    = m_frequency,
        .damping      = m_damping
    };
    m_target_constraint = erhe::physics::IConstraint::create_point_to_point_constraint_unique(
        constraint_settings
    );

    m_target_node_physics->rigid_body()->begin_move();
    world->add_constraint(m_target_constraint.get());
}

auto Physics_tool::on_drag_ready(Scene_view* scene_view) -> bool
{
    if (!is_enabled())
    {
        log_physics->trace("PT not enabled");
        return false;
    }
    if (!acquire_target(scene_view))
    {
        return false;
    }

    begin_point_to_point_constraint();

    log_physics->trace("PT drag {} ready", m_target_mesh->name());
    return true;
}

#if 0
auto Physics_tool::on_force_ready() -> bool
{
    if (!is_enabled())
    {
        return false;
    }
    if (!acquire_target())
    {
        log_physics->trace("Physics tool force - acquire target failed");
        return false;
    }

    begin_point_to_point_constraint();

    log_physics->trace("Physics tool force {} ready", m_target_mesh->name());
    return true;
}
#endif

void Physics_tool::tool_hover(Scene_view* scene_view)
{
    if (scene_view == nullptr)
    {
        return;
    }

    const auto& hover = scene_view->get_hover(Hover_entry::content_slot);
    m_hover_mesh = hover.mesh;
}

auto Physics_tool::on_drag(Scene_view* scene_view) -> bool
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
    if (scene_view == nullptr)
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

    const auto end = m_mode == Physics_tool_mode::Drag
        ? scene_view->get_control_position_in_world_at_distance(m_target_distance)
        : scene_view->get_control_ray_origin_in_world();
    if (!end.has_value())
    {
        return false;
    }

    if (m_mode == Physics_tool_mode::Drag)
    {
        m_target_position_start = glm::vec3{m_target_mesh->world_from_node() * glm::vec4{m_target_position_in_mesh, 1.0f}};
        m_target_position_end   = end.value();
    }
    else
    {
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
        const double distance = glm::distance(end.value(), m_target_position_start);
        if (distance > max_radius * 4.0)
        {
            m_target_position_end = m_target_position_start + m_force_distance * distance * m_to_end_direction;
        }
        else
        {
            m_target_position_end = end.value() + m_force_distance * distance * m_to_start_direction;
        }
        m_target_distance = distance;
    }

    m_constraint_world_point_rigid_body->set_world_transform(
        erhe::physics::Transform{
            glm::mat3{1.0f},
            glm::vec3{m_target_position_end}
        }
    );

    return true;
}

void Physics_tool::tool_render(const Render_context& /*context*/)
{
    ERHE_PROFILE_FUNCTION

    erhe::application::Line_renderer& line_renderer = *m_line_renderer_set->hidden.at(2).get();

    if (m_target_constraint)
    {
        constexpr uint32_t white = 0xffffffffu;
        line_renderer.set_line_color(white);
        line_renderer.set_thickness(4.0f);
        line_renderer.add_lines(
            {
                {
                    m_target_position_start,
                    m_target_position_end
                }
            }
        );
    }

    if (m_target_mesh)
    {
        constexpr uint32_t  white {0xffffffffu};
        constexpr glm::vec3 axis_x{ 1.0f,  0.0f, 0.0f};
        constexpr glm::vec3 axis_y{ 0.0f,  1.0f, 0.0f};
        constexpr glm::vec3 axis_z{ 0.0f,  0.0f, 1.0f};
        const glm::mat4 m = erhe::toolkit::create_translation<float>(
            glm::vec3{m_target_mesh->position_in_world()}
        );
        line_renderer.set_line_color(white);
        line_renderer.set_thickness(10.0f);
        line_renderer.add_lines(
            m,
            {
                { -20.0f * axis_x, 20.0f * axis_x},
                { -20.0f * axis_y, 20.0f * axis_y},
                { -20.0f * axis_z, 20.0f * axis_z}
            }
        );
    }

    //m_line_renderer_set->hidden.set_line_color(0xffff0000u);
    //m_line_renderer_set->hidden.add_lines(
    //    {
    //        {
    //            m_target_position_start,
    //            m_target_position_start + m_to_end_direction
    //        }
    //    }
    //);
    //m_line_renderer_set->hidden.set_line_color(0xff00ff00u);
    //m_line_renderer_set->hidden.add_lines(
    //    {
    //        {
    //            m_target_position_start,
    //            m_target_position_start + m_to_start_direction
    //        }
    //    }
    //);
}

void Physics_tool::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)

    //ImGui::Text("State: %s",     erhe::application::c_state_str[static_cast<int>(m_drag_command.get_tool_state())]);
    //if (m_hover_mesh)
    //{
    //    ImGui::Text("Hover mesh: %s", m_hover_mesh->name().c_str());
    //}
    if (m_target_mesh)
    {
        m_last_target_mesh = m_target_mesh;
    }

    if (!m_last_target_mesh)
    {
        return;
    }

    ImGui::Text("Mesh: %s", m_last_target_mesh->name().c_str());

    float speed = 0.0f;
    std::shared_ptr<Node_physics> node_physics = get_physics_node(m_last_target_mesh.get());
    if (node_physics)
    {
        erhe::physics::IRigid_body* rigid_body = node_physics->rigid_body();
        if (rigid_body != nullptr)
        {
            const glm::vec3 velocity = rigid_body->get_linear_velocity();
            speed = glm::length(velocity);
        }
    }

    //const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
    //ImGui::SliderFloat("Force Distance",  &m_force_distance, -10.0f, 10.0f, "%.2f", logarithmic);
    ImGui::SliderFloat("Depth",     &m_depth,     0.0f, 1.0f);
    ImGui::SliderFloat("Frequency", &m_frequency, 0.0f, 1.0f);
    ImGui::SliderFloat("Damping",   &m_damping,   0.0f, 1.0f);
    //ImGui::SliderFloat("Tau",             &m_tau,             0.0f,   0.1f);
    //ImGui::SliderFloat("Impulse Clamp",   &m_impulse_clamp,   0.0f, 100.0f);
    ImGui::SliderFloat("Override Gravity",         &m_override_gravity,         0.0f, 10.0f);
    ImGui::SliderFloat("Override Friction",        &m_override_friction,        0.0f,  1.0f);
    ImGui::SliderFloat("Override Linear Damping",  &m_override_linear_damping,  0.0f,  1.0f);
    ImGui::SliderFloat("Override Angular Damping", &m_override_angular_damping, 0.0f,  1.0f);

    ImGui::Text("Distance: %f",    m_target_distance);
    ImGui::Text("Target Size: %f", m_target_mesh_size);
    if (m_target_constraint)
    {
        std::string object_position = fmt::format("{}", m_target_position_in_mesh);
        std::string world_position  = fmt::format("{}", m_target_position_end);
        ImGui::Text("Object: %s", object_position.c_str());
        ImGui::Text("World: %s",  world_position.c_str());
    }
#endif
}

} // namespace editor
