#include "tools/physics_tool.hpp"
#include "tools/physics_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "renderers/render_context.hpp"
#include "graphics/icon_set.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/tools.hpp"

#include "erhe/commands/input_arguments.hpp"
#include "erhe/commands/commands.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/renderer/line_renderer.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/iconstraint.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

class Scene_builder;

#pragma region Commands
Physics_tool_drag_command::Physics_tool_drag_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "Physics_tool.drag"}
    , m_context{context}
{
}

void Physics_tool_drag_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        log_physics->trace("PT state not inactive");
        return;
    }

    if (m_context.physics_tool->on_drag_ready()) {
        log_physics->trace("PT set ready");
        set_ready();
    }
}

auto Physics_tool_drag_command::try_call() -> bool
{
    if (get_command_state() == erhe::commands::State::Inactive) {
        return false;
    }

    if (
        m_context.physics_tool->on_drag() &&
        (get_command_state() == erhe::commands::State::Ready)
    ) {
        set_active();
    }

    return get_command_state() == erhe::commands::State::Active;
}

void Physics_tool_drag_command::on_inactive()
{
    log_physics->trace("PT on_inactive");
    if (
        (get_command_state() == erhe::commands::State::Ready ) ||
        (get_command_state() == erhe::commands::State::Active)
    ) {
        m_context.physics_tool->release_target();
    }
}
#pragma endregion Commands

Physics_tool::Physics_tool(
    erhe::commands::Commands& commands,
    Editor_context&           editor_context,
    Editor_message_bus&       editor_message_bus,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool                          {editor_context}
    , m_drag_command                {commands,editor_context}
#if defined(ERHE_XR_LIBRARY_OPENXR)
    , m_drag_redirect_update_command{commands, m_drag_command}
    , m_drag_enable_command         {commands, m_drag_redirect_update_command, 0.3f, 0.2f}
#endif
{
    set_base_priority(c_priority);
    set_description  ("Physics Tool");
    set_flags        (Tool_flags::toolbox);
    set_icon         (icon_set.icons.drag);
    tools.register_tool(this);

    commands.register_command(&m_drag_command);
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::toolkit::Mouse_button_right, true);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = headset_view.get_headset();
    if (headset != nullptr) {
        auto& xr_right = headset->get_actions_right();
        //commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.trigger_click);
        //commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.a_click);
        commands.bind_command_to_xr_float_action(&m_drag_enable_command, xr_right.trigger_value);
        commands.bind_command_to_update         (&m_drag_redirect_update_command);
        m_drag_enable_command.set_host(this);
    }
#else
    static_cast<void>(headset_view);
#endif

    m_motion_mode = erhe::physics::Motion_mode::e_kinematic_non_physical;

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );

    m_drag_command.set_host(this);
}

Physics_tool::~Physics_tool() noexcept
{
    if (m_target_constraint) {
        if (m_physics_world != nullptr) {
            m_physics_world->remove_constraint(m_target_constraint.get());
        }
        m_target_constraint.reset();
    }
}

void Physics_tool::on_message(Editor_message& message)
{
    // Re-implementing here Tool::on_message(message);

    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        //if (m_hover_scene_view != scene_view)
        {
            //Scene_view* old_scene_view = scene_view;
            set_hover_scene_view(message.scene_view);
            //Scene_view* new_scene_view = scene_view;

            if (m_physics_world != nullptr) {
                if (m_constraint_world_point_rigid_body) {
                    m_physics_world->remove_rigid_body(m_constraint_world_point_rigid_body.get());
                    m_constraint_world_point_rigid_body.reset();
                }
                release_target();
                m_physics_world = nullptr;
            }

            Scene_view* scene_view = get_hover_scene_view();
            if (scene_view != nullptr) {
                auto scene_root = scene_view->get_scene_root();
                if (scene_root) {
                    m_physics_world = &scene_root->physics_world();
                    m_constraint_world_point_rigid_body = erhe::physics::IRigid_body::create_rigid_body_shared(
                        erhe::physics::IRigid_body_create_info{
                            .world             = scene_root->physics_world(),
                            .collision_shape   = erhe::physics::ICollision_shape::create_empty_shape_shared(),
                            .mass              = 10.0f,
                            .debug_label       = "Physics Tool",
                            .enable_collisions = false
                        },
                        this
                    );
                    m_physics_world->add_rigid_body(m_constraint_world_point_rigid_body.get());
                }
            }
        }
    }
}

[[nodiscard]] auto Physics_tool::get_scene_root() const -> Scene_root*
{
    Scene_view* scene_view = get_hover_scene_view();
    return (scene_view != nullptr) ? scene_view->get_scene_root().get() : nullptr;
}

[[nodiscard]] auto Physics_tool::get_raytrace_scene() const -> erhe::raytrace::IScene*
{
    Scene_root* scene_root = get_scene_root();
    return (scene_root != nullptr) ? &scene_root->raytrace_scene() : nullptr;
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

void Physics_tool::set_world_from_rigidbody(const glm::mat4& transform)
{
    log_physics->warn("Physics_tool::set_world_from_rigidbody() - This should not happen");
    static_cast<void>(transform);
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

auto Physics_tool::acquire_target() -> bool
{
    log_physics->trace("acquire_target() ...");

    if (m_physics_world == nullptr) {
        log_physics->error("No physics world");
        return false;
    }

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_physics->warn("Cant target: No scene_view");
        return false;
    }

    const auto& content = scene_view->get_hover(Hover_entry::content_slot);
    if (
        !content.valid ||
        !content.position.has_value()
    ) {
        log_physics->warn("Cant target: No content");
        return false;
    }

    const auto p0_opt = scene_view->get_control_ray_origin_in_world();
    if (!p0_opt.has_value()) {
        log_physics->warn("Cant target: No ray origin");
        return false;
    }

    const glm::vec3 view_position   = p0_opt.value();
    const glm::vec3 target_position = glm::mix(
        glm::vec3{content.position.value()},
        glm::vec3{content.mesh->get_node()->position_in_world()},
        m_depth
    );

    m_target_mesh     = content.mesh;
    m_target_distance = glm::distance(view_position, target_position);
    m_target_position_in_mesh = m_target_mesh->get_node()->transform_point_from_world_to_local(
        target_position
    );
    m_target_node_physics = get_node_physics(m_target_mesh->get_node());
    if (!m_target_node_physics) {
        log_physics->warn("Cant target: No physics mesh");
        m_target_mesh.reset();
        return false;
    }

    erhe::physics::IRigid_body* rigid_body = m_target_node_physics->rigid_body();
    if (rigid_body->get_motion_mode() == erhe::physics::Motion_mode::e_static) {
        log_physics->warn("Cant target: Static mesh");
        return false;
    }

    if (m_target_constraint) {
        m_physics_world->remove_constraint(m_target_constraint.get());
        m_target_constraint.reset();
    }

    if (!m_constraint_world_point_rigid_body) {
        return false; // TODO XXX MUSTFIX
    }

    // TODO investigate Jolt damping
    //m_original_linear_damping  = rigid_body->get_linear_damping();
    //m_original_angular_damping = rigid_body->get_angular_damping();
    m_original_friction        = rigid_body->get_friction();
    m_original_gravity         = rigid_body->get_gravity_factor();

    //rigid_body->set_damping(
    //    m_override_linear_damping,
    //    m_override_angular_damping
    //);
    rigid_body->set_friction      (m_override_friction);
    rigid_body->set_gravity_factor(m_override_gravity);

    rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
    rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});

    m_target_position_end = glm::vec3{
        m_target_mesh->get_node()->world_from_node() * glm::vec4{m_target_position_in_mesh, 1.0f}
    };

    log_physics->trace("PT pos in mesh {}", m_target_position_in_mesh);
    log_physics->trace("PT drag {} ready @ {}", m_target_mesh->get_name(), m_target_position_end);

    move_drag_point_instant(m_target_position_end);

    const erhe::physics::Point_to_point_constraint_settings constraint_settings{
        .rigid_body_a = m_target_node_physics->rigid_body(),
        .rigid_body_b = m_constraint_world_point_rigid_body.get(),
        .pivot_in_a   = m_target_position_in_mesh,
        .pivot_in_b   = glm::vec3{0.0f, 0.0f, 0.0f},
        .frequency    = m_frequency,
        .damping      = m_damping
    };

    m_target_node_physics->rigid_body()->begin_move();

    m_target_constraint = erhe::physics::IConstraint::create_point_to_point_constraint_unique(
        constraint_settings
    );
    m_physics_world->add_constraint(m_target_constraint.get());

    return true;
}

void Physics_tool::move_drag_point_instant(glm::vec3 position)
{
    m_motion_mode = erhe::physics::Motion_mode::e_kinematic_non_physical;
    m_constraint_world_point_rigid_body->set_world_transform(
        erhe::physics::Transform{
            glm::mat3{1.0f},
            position
        }
    );
}

void Physics_tool::move_drag_point_kinematic(glm::vec3 position)
{
    m_motion_mode = erhe::physics::Motion_mode::e_kinematic_physical;
    m_constraint_world_point_rigid_body->set_world_transform(
        erhe::physics::Transform{
            glm::mat3{1.0f},
            position
        }
    );
}

void Physics_tool::release_target()
{
    log_physics->trace("PT Target released");

    if (m_target_constraint) {
        if (m_physics_world != nullptr) {
            m_physics_world->remove_constraint(m_target_constraint.get());
        }
        m_target_constraint.reset();
    }

    if (m_target_node_physics) {
        erhe::physics::IRigid_body* rigid_body = m_target_node_physics->rigid_body();
        //rigid_body->set_damping(
        //    m_original_linear_damping,
        //    m_original_angular_damping
        //);
        rigid_body->set_friction(m_original_friction);
        rigid_body->set_gravity_factor(m_original_gravity);
        //rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        //rigid_body->set_linear_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        rigid_body->end_move();
        m_target_node_physics.reset();
    }

    m_target_mesh.reset();

    m_target_distance         = 1.0;
    m_target_position_in_mesh = glm::vec3{0.0, 0.0, 0.0};
    m_target_position_end     = glm::vec3{0.0, 0.0, 0.0};
    m_to_end_direction        = glm::vec3{0.0};
    m_to_start_direction      = glm::vec3{0.0};
    m_target_mesh_size        = 0.0;
}

auto Physics_tool::on_drag_ready() -> bool
{
    if (!acquire_target()) {
        return false;
    }

    log_physics->trace("PT drag {} ready", m_target_mesh->get_name());
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

    log_physics->trace("Physics tool force {} ready", m_target_mesh->get_name());
    return true;
}
#endif

void Physics_tool::tool_hover(Scene_view* scene_view)
{
    if (scene_view == nullptr) {
        return;
    }

    const auto& hover = scene_view->get_hover(Hover_entry::content_slot);
    m_hover_mesh = hover.mesh;
}

auto Physics_tool::on_drag() -> bool
{
    if (m_physics_world == nullptr) {
        return false;
    }
    if (!m_physics_world->is_physics_updates_enabled()) {
        return false;
    }

    Scene_view* scene_view = get_hover_scene_view();
    if (!m_target_mesh) {
        return false;
    }
    if (!m_target_constraint) {
        return false;
    }

    const auto end = m_mode == Physics_tool_mode::Drag
        ? scene_view->get_control_position_in_world_at_distance(m_target_distance)
        : scene_view->get_control_ray_origin_in_world();
    if (!end.has_value()) {
        return false;
    }

    if (m_mode == Physics_tool_mode::Drag) {
        m_target_position_start = glm::vec3{m_target_mesh->get_node()->world_from_node() * glm::vec4{m_target_position_in_mesh, 1.0f}};
        m_target_position_end   = end.value();
    } else {
        m_target_position_start = glm::vec3{m_target_mesh->get_node()->world_from_node() * glm::vec4{m_target_position_in_mesh, 1.0f}};

        float max_radius = 0.0f;
        for (const auto& primitive : m_target_mesh->mesh_data.primitives) {
            max_radius = std::max(
                max_radius,
                primitive.gl_primitive_geometry.bounding_sphere.radius
            );
        }
        m_target_mesh_size   = max_radius;
        m_to_end_direction   = glm::normalize(end.value() - m_target_position_start);
        m_to_start_direction = glm::normalize(m_target_position_start - end.value());
        const float distance = glm::distance(end.value(), m_target_position_start);
        if (distance > max_radius * 4.0f) {
            m_target_position_end = m_target_position_start + m_force_distance * distance * m_to_end_direction;
        } else {
            m_target_position_end = end.value() + m_force_distance * distance * m_to_start_direction;
        }
        m_target_distance = distance;
    }

    move_drag_point_kinematic(m_target_position_end);

    if (m_target_node_physics) {
        // TODO investigate jolt damping
        erhe::physics::IRigid_body* rigid_body = m_target_node_physics->rigid_body();
        glm::vec3 angular_velocity = rigid_body->get_angular_velocity();
        glm::vec3 linear_velocity  = rigid_body->get_linear_velocity();

        rigid_body->set_angular_velocity(angular_velocity * (1.0f - m_override_angular_damping));
        rigid_body->set_linear_velocity(linear_velocity * (1.0f - m_override_linear_damping));
    }

    return true;
}

void Physics_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        release_target();
    }
}

void Physics_tool::tool_render(const Render_context&)
{
    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Line_renderer& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

#if 0 // This is currently too slow, at least in debug build
    if (m_target_mesh) {
        auto* raytrace_scene = get_raytrace_scene();
        if (raytrace_scene != nullptr) {
            erhe::raytrace::Ray ray{
                .origin    = m_target_mesh->get_node()->position_in_world(),
                .t_near    = 0.0f,
                .direction = glm::vec3{0.0f, -1.0f, 0.0f},
                .time      = 0.0f,
                .t_far     = 9999.0f,
                .mask      = Raytrace_node_mask::content,
                .id        = 0,
                .flags     = 0
            };

            erhe::raytrace::Hit hit;
            if (project_ray(raytrace_scene, m_target_mesh.get(), ray, hit)) {
                draw_ray_hit(line_renderer, ray, hit, m_ray_hit_style);
            }
        }
    }
#endif

    if (m_target_constraint) {
        constexpr glm::vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        line_renderer.set_thickness(4.0f);
        line_renderer.add_lines(
            white,
            {
                {
                    m_target_position_start,
                    m_target_position_end
                }
            }
        );
    }

    constexpr glm::vec3 axis_x{ 1.0f,  0.0f, 0.0f};
    constexpr glm::vec3 axis_y{ 0.0f,  1.0f, 0.0f};
    constexpr glm::vec3 axis_z{ 0.0f,  0.0f, 1.0f};

    //if (m_target_mesh)
    //{
    //    constexpr uint32_t  white {0xffff00ffu};
    //    const glm::mat4 m = erhe::toolkit::create_translation<float>(
    //        glm::vec3{m_target_mesh->position_in_world()}
    //    );
    //    line_renderer.set_line_color(white);
    //    line_renderer.set_thickness(10.0f);
    //    line_renderer.add_lines(
    //        m,
    //        {
    //            //{ -20.0f * axis_x, 20.0f * axis_x},
    //            { -20.0f * axis_y, 20.0f * axis_y}
    //            //{ -20.0f * axis_z, 20.0f * axis_z}
    //        }
    //    );
    //}

    if (m_show_drag_body) {
        const erhe::physics::Transform transform = m_constraint_world_point_rigid_body->get_world_transform();
        {
            const glm::vec4 half_red  {0.5f, 0.0f, 0.0f, 0.5f};
            const glm::vec4 half_green{0.0f, 0.5f, 0.0f, 0.5f};
            const glm::vec4 half_blue {0.0f, 0.0f, 0.5f, 0.5f};
            constexpr glm::vec3 O{ 0.0f };
            glm::mat4 m{transform.basis};
            m[3] = glm::vec4{
                transform.origin.x,
                transform.origin.y,
                transform.origin.z,
                1.0f
            };
            line_renderer.add_lines( m, half_red,   {{ O, axis_x }} );
            line_renderer.add_lines( m, half_green, {{ O, axis_y }} );
            line_renderer.add_lines( m, half_blue,  {{ O, axis_z }} );
        }
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

    if (m_target_mesh) {
        m_last_target_mesh = m_target_mesh;
    }

    if (!m_last_target_mesh) {
        return;
    }

    ImGui::Checkbox("Show Drag Body", &m_show_drag_body);
    ImGui::Text("Mesh: %s", m_last_target_mesh->get_name().c_str());

    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
    ImGui::SliderFloat("Depth",     &m_depth,     0.0f, 1.0f);
    ImGui::SliderFloat("Frequency", &m_frequency, 0.0f, 100.0f, "%.3f", logarithmic);
    ImGui::SliderFloat("Damping",   &m_damping,   0.0f, 1.0f);
    ImGui::Separator();
    ImGui::Text("Override:");
    ImGui::SliderFloat("Gravity",      &m_override_gravity,         0.0f, 10.0f);
    ImGui::SliderFloat("Friction",     &m_override_friction,        0.0f,  1.0f);
    ImGui::SliderFloat("Linear Damp",  &m_override_linear_damping,  0.0f,  1.0f);
    ImGui::SliderFloat("Angular Damp", &m_override_angular_damping, 0.0f,  1.0f);
    ImGui::Separator();
    ImGui::Text("Info:");
    ImGui::Text("Distance: %f",    m_target_distance);
    ImGui::Text("Target Size: %f", m_target_mesh_size);
    if (m_target_constraint) {
        std::string object_position = fmt::format("{}", m_target_position_in_mesh);
        std::string world_position  = fmt::format("{}", m_target_position_end);
        const erhe::physics::Transform transform = m_constraint_world_point_rigid_body->get_world_transform();
        std::stringstream ss;
        ss << fmt::format("{}", transform.origin);
        ImGui::Text("Drag point body pos: %s", ss.str().c_str());
        ImGui::Text("Object: %s", object_position.c_str());
        ImGui::Text("World: %s",  world_position.c_str());
    }
    ImGui::ColorEdit4 ("Ray Color",     &m_ray_hit_style.ray_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Ray Length",    &m_ray_hit_style.ray_length,    0.0f,  1.0f);
    ImGui::SliderFloat("Ray Thickness", &m_ray_hit_style.ray_thickness, 0.0f, 10.0f);
    ImGui::ColorEdit4 ("Hit Color",     &m_ray_hit_style.hit_color.x, ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Hit Size",      &m_ray_hit_style.hit_size,      0.0f,  1.0f);
    ImGui::SliderFloat("Hit Thickness", &m_ray_hit_style.hit_thickness, 0.0f, 10.0f);
#endif
}

} // namespace editor
