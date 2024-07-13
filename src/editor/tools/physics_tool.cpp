#include "tools/physics_tool.hpp"
#include "tools/physics_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_settings.hpp"
#include "renderers/render_context.hpp"
#include "graphics/icon_set.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_renderer/line_renderer.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor {

class Scene_builder;

#pragma region Commands
Physics_tool_drag_command::Physics_tool_drag_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Physics_tool.drag"}
    , m_context{context}
{
}

void Physics_tool_drag_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        //log_physics->trace("PT state not inactive");
        return;
    }

    if (m_context.physics_tool->on_drag_ready()) {
        //log_physics->trace("PT set ready");
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
    //log_physics->trace("PT on_inactive");
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
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::window::Mouse_button_right, true);
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

    using namespace erhe::bit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        if (get_hover_scene_view() != message.scene_view) {
            set_hover_scene_view(message.scene_view);

            if (m_physics_world != nullptr) {
                if (m_target_constraint) {
                    m_physics_world->remove_constraint(m_target_constraint.get());
                    m_target_constraint.reset();
                }
                release_target();
                m_physics_world = nullptr;
            }

            Scene_view* scene_view = get_hover_scene_view();
            if (scene_view != nullptr) {
                auto scene_root = scene_view->get_scene_root();
                if (scene_root) {
                    m_physics_world = &scene_root->get_physics_world();
                }
            }
        }
    }
}

auto Physics_tool::get_scene_root() const -> Scene_root*
{
    Scene_view* scene_view = get_hover_scene_view();
    return (scene_view != nullptr) ? scene_view->get_scene_root().get() : nullptr;
}

auto Physics_tool::get_mode() const -> Physics_tool_mode
{
    return m_mode;
}

void Physics_tool::set_mode(Physics_tool_mode value)
{
    m_mode = value;
}

auto Physics_tool::acquire_target() -> bool
{
    if (m_physics_world == nullptr) {
        log_physics->error("No physics world");
        return false;
    }

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_physics->warn("Can't target: No scene_view");
        return false;
    }

    const auto& content = scene_view->get_hover(Hover_entry::content_slot);
    if (
        !content.valid ||
        (content.mesh == nullptr) ||
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

    auto target_mesh = std::static_pointer_cast<erhe::scene::Mesh>(content.mesh->shared_from_this());
    auto target_node_physics = get_node_physics(target_mesh->get_node());
    if (!target_node_physics) {
        log_physics->warn("Cant target: No physics mesh");
        return false;
    }

    erhe::physics::IRigid_body* rigid_body = target_node_physics->get_rigid_body();
    if (rigid_body->get_motion_mode() == erhe::physics::Motion_mode::e_static) {
        log_physics->warn("Cant target: Static mesh");
        return false;
    }

    m_target_mesh         = target_mesh;
    m_target_node_physics = target_node_physics;

    const auto collision_shape = rigid_body->get_collision_shape();
    const glm::vec3 rigid_body_center_of_mass = collision_shape->get_center_of_mass();

    const glm::vec3 view_position = p0_opt.value();

    // Grab position is between pointed position and node center based on "grab depth" parameter
    m_grab_position_world = glm::mix(
        glm::vec3{content.position.value()},
        glm::vec3{content.mesh->get_node()->position_in_world()},
        m_depth
    );
    m_goal_position_in_world = m_grab_position_world;

    m_target_distance       = glm::distance(view_position, m_goal_position_in_world);
    m_grab_position_in_node = m_target_mesh->get_node()->transform_point_from_world_to_local(
        m_goal_position_in_world
    );
    m_grab_position_in_collision_shape = m_grab_position_in_node - rigid_body_center_of_mass;

    m_target_node_physics->markers.clear();
    m_target_node_physics->markers.push_back(m_grab_position_in_node);

    glm::vec3 node_position = glm::vec3{m_target_mesh->get_node()->position_in_world()};
    const glm::mat4 rigid_body_transform = rigid_body->get_world_transform();
    glm::vec3 rigid_body_position = glm::vec3{rigid_body_transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    //log_physics->trace("Node pos: {}", node_position);
    //log_physics->trace("Rigid body pos: {}", rigid_body_position);

    if (m_target_constraint) {
        m_physics_world->remove_constraint(m_target_constraint.get());
        m_target_constraint.reset();
    }

    if (m_constraint_world_point_rigid_body) {
        m_physics_world->remove_rigid_body(m_constraint_world_point_rigid_body.get());
        m_constraint_world_point_rigid_body.reset();
    }
    m_constraint_world_point_rigid_body = m_physics_world->create_rigid_body_shared(
        erhe::physics::IRigid_body_create_info{
            .collision_shape   = erhe::physics::ICollision_shape::create_empty_shape_shared(),
            .mass              = 10.0f,
            .debug_label       = "Physics Tool",
            .enable_collisions = false,
            .motion_mode       = erhe::physics::Motion_mode::e_kinematic_non_physical
        }
    );
    m_physics_world->add_rigid_body(m_constraint_world_point_rigid_body.get());

    // TODO investigate Jolt damping
    m_original_linear_damping  = rigid_body->get_linear_damping();
    m_original_angular_damping = rigid_body->get_angular_damping();
    m_original_friction        = rigid_body->get_friction();
    m_original_gravity         = rigid_body->get_gravity_factor();

    if (m_override_damping_enable) {
        rigid_body->set_damping(m_override_linear_damping, m_override_angular_damping);
    }
    if (m_override_friction_enable) {
        rigid_body->set_friction(m_override_friction_value);
    }
    if (m_override_gravity_enable) {
        rigid_body->set_gravity_factor(m_override_gravity_value);
    }

    // When grabbing, objects instantly stop
    rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
    rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});

    move_drag_point_instant(m_goal_position_in_world);

    const erhe::physics::Point_to_point_constraint_settings constraint_settings{
        .rigid_body_a = m_target_node_physics->get_rigid_body(),
        .rigid_body_b = m_constraint_world_point_rigid_body.get(),
        .pivot_in_a   = m_grab_position_in_collision_shape, // shape center of mass taken into account
        .pivot_in_b   = glm::vec3{0.0f, 0.0f, 0.0f},
        .frequency    = m_frequency,
        .damping      = m_damping
    };

    m_target_node_physics->get_rigid_body()->begin_move();

    m_target_constraint = erhe::physics::IConstraint::create_point_to_point_constraint_unique(
        constraint_settings
    );
    m_physics_world->add_constraint(m_target_constraint.get());

    return true;
}

void Physics_tool::move_drag_point_instant(glm::vec3 position)
{
    m_constraint_world_point_rigid_body->set_motion_mode(erhe::physics::Motion_mode::e_kinematic_non_physical);
    m_constraint_world_point_rigid_body->set_world_transform(
        erhe::physics::Transform{
            glm::mat3{1.0f},
            position
        }
    );
}

void Physics_tool::move_drag_point_kinematic(glm::vec3 position)
{
    m_constraint_world_point_rigid_body->set_motion_mode(erhe::physics::Motion_mode::e_kinematic_physical);
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
        erhe::physics::IRigid_body* rigid_body = m_target_node_physics->get_rigid_body();
        rigid_body->set_damping(m_original_linear_damping, m_original_angular_damping);
        rigid_body->set_friction(m_original_friction);
        rigid_body->set_gravity_factor(m_original_gravity);
        //rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        //rigid_body->set_linear_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        rigid_body->end_move();
        m_target_node_physics.reset();
    }

    m_target_mesh.reset();

    m_target_distance                  = 1.0;
    m_grab_position_in_node            = glm::vec3{0.0, 0.0, 0.0};
    m_grab_position_in_collision_shape = glm::vec3{0.0, 0.0, 0.0};
    m_goal_position_in_world           = glm::vec3{0.0, 0.0, 0.0};
    m_to_end_direction                 = glm::vec3{0.0};
    m_to_start_direction               = glm::vec3{0.0};
    m_target_mesh_size                 = 0.0;

    if (m_constraint_world_point_rigid_body) {
        m_physics_world->remove_rigid_body(m_constraint_world_point_rigid_body.get());
        m_constraint_world_point_rigid_body.reset();
    }
}

auto Physics_tool::on_drag_ready() -> bool
{
    if (!acquire_target()) {
        return false;
    }

    log_physics->trace("PT drag {} ready", m_target_mesh->get_name());
    return true;
}

void Physics_tool::tool_hover(Scene_view* scene_view)
{
    if (scene_view == nullptr) {
        return;
    }

    const auto& hover = scene_view->get_hover(Hover_entry::content_slot);
    m_hover_mesh = std::static_pointer_cast<erhe::scene::Mesh>(hover.mesh->shared_from_this());
}

auto Physics_tool::on_drag() -> bool
{
    if (m_physics_world == nullptr) {
        return false;
    }
    if (!m_context.editor_settings->physics.dynamic_enable) {
        return false;
    }

    if (!m_target_node_physics) {
        return false;
    }

    if (!m_target_mesh) {
        return false;
    }
    if (!m_target_constraint) {
        return false;
    }

    Scene_view* scene_view = get_hover_scene_view();
    const auto end = m_mode == Physics_tool_mode::Drag
        ? scene_view->get_control_position_in_world_at_distance(m_target_distance)
        : scene_view->get_control_ray_origin_in_world();
    if (!end.has_value()) {
        return false;
    }

    if (m_mode == Physics_tool_mode::Drag) {
        m_grab_position_world    = glm::vec3{m_target_mesh->get_node()->world_from_node() * glm::vec4{m_grab_position_in_node, 1.0f}};
        m_goal_position_in_world = end.value();
    } else {
        m_grab_position_world = glm::vec3{m_target_mesh->get_node()->world_from_node() * glm::vec4{m_grab_position_in_node, 1.0f}};

        erhe::math::Bounding_box mesh_bounding_box;
        for (const erhe::primitive::Primitive& primitive : m_target_mesh->get_primitives()) {
            erhe::math::Bounding_box primitive_bounding_box = primitive.get_bounding_box();
            if (primitive_bounding_box.is_valid()) {
                mesh_bounding_box.include(primitive_bounding_box);
            }
        }
        m_target_mesh_size   = glm::length(mesh_bounding_box.diagonal());
        m_to_end_direction   = glm::normalize(end.value() - m_grab_position_world);
        m_to_start_direction = glm::normalize(m_grab_position_world - end.value());
        const float distance = glm::distance(end.value(), m_grab_position_world);
        if (distance > m_target_mesh_size * 4.0f) {
            m_goal_position_in_world = m_grab_position_world + m_force_distance * distance * m_to_end_direction;
        } else {
            m_goal_position_in_world = end.value() + m_force_distance * distance * m_to_start_direction;
        }
        m_target_distance = distance;
    }

    move_drag_point_kinematic(m_goal_position_in_world);

    // TODO investigate jolt damping
    if (m_extra_damping_enable) {
        erhe::physics::IRigid_body* rigid_body = m_target_node_physics->get_rigid_body();
        glm::vec3 angular_velocity = rigid_body->get_angular_velocity();
        glm::vec3 linear_velocity  = rigid_body->get_linear_velocity();

        rigid_body->set_angular_velocity(angular_velocity * (1.0f - m_extra_angular_damping));
        rigid_body->set_linear_velocity(linear_velocity * (1.0f - m_extra_linear_damping));
    }

    return true;
}

void Physics_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        release_target();
    }
}

void Physics_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    erhe::renderer::Line_renderer& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    // TODO Make sure this has good enough perf, disable if not.
    if (m_target_mesh) {
        erhe::raytrace::IScene& rt_scene = context.scene_view.get_scene_root()->get_raytrace_scene();
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
        if (project_ray(&rt_scene, m_target_mesh.get(), ray, hit)) {
            draw_ray_hit(line_renderer, ray, hit, m_ray_hit_style);
        }
    }

    if (m_target_constraint) {
        const float d = 0.05f;
        const glm::vec3 dx{d, 0.0f, 0.0f};
        const glm::vec3 dy{0.0f, d, 0.0f};
        const glm::vec3 dz{0.0f, 0.0f, d};

        constexpr glm::vec4 white{6.0f, 1.5f, 1.5f, 1.0f};
        line_renderer.set_thickness(4.0f);
        line_renderer.add_lines(
            white,
            { { m_grab_position_world, m_goal_position_in_world } }
        );
    }

    constexpr glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
    constexpr glm::vec3 axis_z{0.0f, 0.0f, 1.0f};

    if (m_show_drag_body && m_constraint_world_point_rigid_body) {
        const glm::mat4 m = m_constraint_world_point_rigid_body->get_world_transform();
        const glm::vec4 half_red  {0.5f, 0.0f, 0.0f, 0.5f};
        const glm::vec4 half_green{0.0f, 0.5f, 0.0f, 0.5f};
        const glm::vec4 half_blue {0.0f, 0.0f, 0.5f, 0.5f};
        constexpr glm::vec3 O{ 0.0f };
        line_renderer.add_lines( m, half_red,   {{ O, axis_x }} );
        line_renderer.add_lines( m, half_green, {{ O, axis_y }} );
        line_renderer.add_lines( m, half_blue,  {{ O, axis_z }} );
    }
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
    ImGui::SliderFloat("Depth",                &m_depth,     0.0f, 1.0f);
    ImGui::SliderFloat("Constraint Frequency", &m_frequency, 0.0f, 100.0f, "%.3f", logarithmic);
    ImGui::SliderFloat("Constraint Damping",   &m_damping,   0.0f, 1.0f);

    ImGui::Separator();

    ImGui::Checkbox   ("Override Gravity", &m_override_gravity_enable);
    if (!m_override_gravity_enable) {
        ImGui::BeginDisabled();
    }
    ImGui::SliderFloat("Gravity",  &m_override_gravity_value,  0.0f, 10.0f);
    if (!m_override_gravity_enable) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    ImGui::Checkbox   ("Override Friction", &m_override_friction_enable);
    if (!m_override_friction_enable) {
        ImGui::BeginDisabled();
    }
    ImGui::SliderFloat("Friction", &m_override_friction_value, 0.0f,  1.0f);
    if (!m_override_friction_enable) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (ImGui::TreeNodeEx("Damping Override", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
        ImGui::Checkbox   ("Override Damping", &m_override_damping_enable);
        if (!m_override_damping_enable) {
            ImGui::BeginDisabled();
        }
        ImGui::SliderFloat("Override Linear Damp",  &m_override_linear_damping,  0.0f, 1.0f);
        ImGui::SliderFloat("Override Angular Damp", &m_override_angular_damping, 0.0f, 1.0f);
        if (!m_override_damping_enable) {
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        ImGui::Checkbox   ("Extra Damping", &m_extra_damping_enable);
        if (!m_extra_damping_enable) {
            ImGui::BeginDisabled();
        }
        ImGui::SliderFloat("Extra Linear Damp",  &m_extra_linear_damping,  0.0f, 1.0f);
        ImGui::SliderFloat("Extra Angular Damp", &m_extra_angular_damping, 0.0f, 1.0f);
        if (!m_extra_damping_enable) {
            ImGui::EndDisabled();
        }
        ImGui::TreePop();
    }

    ImGui::Separator();

    ImGui::Text("Info:");
    ImGui::Text("Distance: %f",    m_target_distance);
    ImGui::Text("Target Size: %f", m_target_mesh_size);
    if (m_target_constraint) {
        const glm::mat4 transform = m_constraint_world_point_rigid_body->get_world_transform();
        std::string constraint_position      = fmt::format("{}", glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}});
        std::string node_position            = fmt::format("{}", m_grab_position_in_node);
        std::string collision_shape_position = fmt::format("{}", m_grab_position_in_collision_shape);
        std::string world_position           = fmt::format("{}", m_goal_position_in_world);
        ImGui::Text("Constraint pos: %s", constraint_position.c_str());
        ImGui::Text("Grab point in Node: %s", node_position.c_str());
        ImGui::Text("Grab point in Collision Shape: %s", collision_shape_position.c_str());
        ImGui::Text("Grab point in World: %s", world_position.c_str());
    }
    if (m_target_node_physics) {
        auto* rigid_body = m_target_node_physics->get_rigid_body();
        if (rigid_body != nullptr) {
            const glm::mat4 transform = rigid_body->get_world_transform();
            std::string pos = fmt::format("{}", glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}});
            const auto motion_mode = rigid_body->get_motion_mode();
            const auto i = static_cast<int>(motion_mode);
            ImGui::Text("Target Node Pos: %s", pos.c_str());
            ImGui::Text("Target motion mode: %s", erhe::physics::c_motion_mode_strings[i]);
            const float mass = rigid_body->get_mass();
            ImGui::Text("Target Mass: %.2f", mass);
            const bool is_active = rigid_body->is_active();
            ImGui::Text("Target Is Active: %s", is_active ? "true" : "false");
        }
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
