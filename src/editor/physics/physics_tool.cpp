#include "physics/physics_tool.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "renderers/render_context.hpp"
#include "graphics/icon_set.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"

#include "app_message.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <imgui/imgui.h>

namespace editor {

class Scene_builder;

#pragma region Commands
Physics_tool_drag_command::Physics_tool_drag_command(erhe::commands::Commands& commands, App_context& context)
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
    App_context&              context,
    App_message_bus&          app_message_bus,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool                          {context, tools, Tool_flags::toolbox}
    , m_drag_command                {commands, context}
#if defined(ERHE_XR_LIBRARY_OPENXR)
    , m_drag_redirect_update_command{commands, m_drag_command}
    , m_drag_enable_command         {commands, m_drag_redirect_update_command, 0.3f, 0.2f}
#endif
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(c_priority);
    set_description  ("Physics Tool");
    set_icon         (icon_set.custom_icons, icon_set.icons.drag);

    commands.register_command(&m_drag_command);
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::window::Mouse_button_right, true);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        //commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.trigger_click);
        //commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.a_click);
        commands.bind_command_to_xr_float_action(&m_drag_enable_command, xr_right->trigger_value);
        commands.bind_command_to_update         (&m_drag_redirect_update_command);
        m_drag_enable_command.set_host(this);
    }
#else
    static_cast<void>(headset_view);
#endif

    m_motion_mode = erhe::physics::Motion_mode::e_kinematic_non_physical;

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            on_message(message);
        }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );

    m_drag_command.set_host(this);
}

Physics_tool::~Physics_tool() noexcept
{
    m_drag_constraint.detach();
}

void Physics_tool::on_message(Hover_scene_view_message& message)
{
    clear_destroyed_scene_view(message);
    if (get_hover_scene_view() != message.scene_view) {
        set_hover_scene_view(message.scene_view);

        if (m_physics_world != nullptr) {
            if (!m_scripted_drag) {
                release_target();
            }
            m_physics_world = nullptr;
        }

        Scene_view* scene_view = get_hover_scene_view();
        if (scene_view != nullptr) {
            auto scene_root = scene_view->get_scene_root();
            if (scene_root && scene_root->has_physics_world()) {
                m_physics_world = &scene_root->get_physics_world();
            }
        }
    }
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
    std::shared_ptr<erhe::scene::Mesh> scene_mesh = content.scene_mesh_weak.lock();
    if (
        !content.valid ||
        !scene_mesh ||
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

    const std::shared_ptr<Scene_root> scene_root = scene_view->get_scene_root();
    if (!scene_root || !scene_root->has_physics_world()) {
        log_physics->warn("Cant target: No physics world in the hovered scene");
        return false;
    }

    // Grab position is between pointed position and node center based on "grab depth" parameter
    const glm::vec3 grab_position_in_world = glm::mix(
        glm::vec3{content.position.value()},
        glm::vec3{scene_mesh->position_in_world()},
        m_depth
    );
    if (!begin_drag(*scene_root.get(), scene_mesh, grab_position_in_world)) {
        return false;
    }
    m_target_distance = glm::distance(p0_opt.value(), m_goal_position_in_world);
    return true;
}

auto Physics_tool::begin_drag(
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Mesh>& target_mesh,
    const glm::vec3                           grab_position_in_world
) -> bool
{
    erhe::scene::Node* target_node = target_mesh.get();
    ERHE_VERIFY(target_node != nullptr);

    auto target_node_physics = erhe::scene::get_attachment<Node_physics>(target_node);
    if (!target_node_physics) {
        log_physics->warn("Cant target: No physics mesh");
        return false;
    }

    erhe::physics::IRigid_body* rigid_body = target_node_physics->get_rigid_body();
    if ((rigid_body == nullptr) || (rigid_body->get_motion_mode() == erhe::physics::Motion_mode::e_static)) {
        log_physics->warn("Cant target: Static mesh");
        return false;
    }

    m_drag_constraint.detach();

    m_target_mesh         = target_mesh;
    m_target_node_physics = target_node_physics;

    const auto collision_shape = rigid_body->get_collision_shape();
    const glm::vec3 rigid_body_center_of_mass = collision_shape->get_center_of_mass();

    m_grab_position_world    = grab_position_in_world;
    m_goal_position_in_world = m_grab_position_world;
    m_grab_position_in_node  = m_target_mesh->transform_point_from_world_to_local(m_goal_position_in_world);
    m_grab_position_in_collision_shape = m_grab_position_in_node - rigid_body_center_of_mass;
    m_center_of_mass_in_node = rigid_body_center_of_mass;

    m_target_node_physics->markers.clear();
    m_target_node_physics->markers.push_back(m_grab_position_in_node);

    m_original_linear_damping  = rigid_body->get_linear_damping();
    m_original_angular_damping = rigid_body->get_angular_damping();
    m_original_friction        = rigid_body->get_friction();
    m_original_gravity         = rigid_body->get_gravity_factor();

    const Physics_drag_body_values values_before_tool{
        .linear_damping  = m_original_linear_damping,
        .angular_damping = m_original_angular_damping,
        .friction        = m_original_friction,
        .gravity_factor  = m_original_gravity
    };
    const char* const mode_name = (m_mode == Physics_tool_mode::Drag) ? "drag" : ((m_mode == Physics_tool_mode::Push) ? "push" : "pull");

    m_target_jointed = scene_root.is_jointed_rigid_body(rigid_body);
    if (m_target_jointed) {
        // A body held by a joint: the same bounded spring as the Transform
        // tool's drag, pivot at the center of mass so the pull cannot torque
        // the joint's locked axes, and none of the tool's body overrides or
        // the grab-time velocity reset (they are not physical for a rig).
        m_overrides_applied = false;
        const glm::vec3 center_of_mass_in_world = glm::vec3{
            m_target_mesh->world_from_node() * glm::vec4{m_center_of_mass_in_node, 1.0f}
        };
        m_drag_constraint.attach(
            scene_root.get_physics_world(),
            *rigid_body,
            glm::vec3{0.0f, 0.0f, 0.0f},
            center_of_mass_in_world,
            make_jointed_body_drag_settings(rigid_body->get_mass()),
            Physics_drag_monitor_info{
                .monitor            = &scene_root.get_physics_drag_monitor(),
                .tool_name          = "physics tool",
                .node               = target_node,
                .values_before_tool = values_before_tool,
                .tool_details = fmt::format(
                    "mode {}, jointed body: spring pivot at center of mass {}, grab in node {} (goal offset by grab -> center of mass each frame), no body overrides, no velocity reset, no extra damping, drag point teleported each frame",
                    mode_name, m_center_of_mass_in_node, m_grab_position_in_node
                )
            },
            scene_root.get_node_joints()
        );
        return true;
    }

    if (m_override_damping_enable) {
        rigid_body->set_damping(m_override_linear_damping, m_override_angular_damping);
    }
    if (m_override_friction_enable) {
        rigid_body->set_friction(m_override_friction_value);
    }
    if (m_override_gravity_enable) {
        rigid_body->set_gravity_factor(m_override_gravity_value);
    }
    m_overrides_applied = true;

    // When grabbing, objects instantly stop
    rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
    rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});

    m_drag_constraint.attach(
        scene_root.get_physics_world(),
        *rigid_body,
        m_grab_position_in_collision_shape, // shape center of mass taken into account
        m_goal_position_in_world,
        Physics_drag_constraint_settings{
            .frequency = m_frequency,
            .damping   = m_damping
        },
        Physics_drag_monitor_info{
            .monitor            = &scene_root.get_physics_drag_monitor(),
            .tool_name          = "physics tool",
            .node               = target_node,
            .values_before_tool = values_before_tool,
            .tool_details = fmt::format(
                "mode {}, depth {:.3f}, grab in node {}, velocities zeroed at grab, override damping {} ({:.3f}, {:.3f}), override friction {} ({:.3f}), override gravity {} ({:.3f}), extra per-frame velocity damping {} (linear {:.3f}, angular {:.3f}), drag point moved as kinematic each on_drag",
                mode_name,
                m_depth, m_grab_position_in_node,
                m_override_damping_enable, m_override_linear_damping, m_override_angular_damping,
                m_override_friction_enable, m_override_friction_value,
                m_override_gravity_enable, m_override_gravity_value,
                m_extra_damping_enable, m_extra_linear_damping, m_extra_angular_damping
            )
        },
        scene_root.get_node_joints()
    );

    return true;
}

auto Physics_tool::begin_scripted_drag(
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const glm::vec3                           grab_point_in_world
) -> bool
{
    if (m_scripted_drag || m_drag_constraint.is_attached()) {
        log_physics->warn("Physics tool scripted drag refused: a drag is active");
        return false;
    }
    if (!mesh || !scene_root.has_physics_world()) {
        return false;
    }
    if (!begin_drag(scene_root, mesh, grab_point_in_world)) {
        return false;
    }
    m_scripted_drag = true;
    return true;
}

void Physics_tool::step_scripted_drag(const glm::vec3 goal_in_world)
{
    if (!m_scripted_drag || !m_target_mesh || !m_drag_constraint.is_attached()) {
        return;
    }
    m_grab_position_world    = glm::vec3{m_target_mesh->world_from_node() * glm::vec4{m_grab_position_in_node, 1.0f}};
    m_goal_position_in_world = goal_in_world;
    apply_drag_goal();
}

auto Physics_tool::is_scripted_drag_active() const -> bool
{
    return m_scripted_drag;
}

auto Physics_tool::is_target_jointed() const -> bool
{
    return m_target_jointed;
}

auto Physics_tool::get_drag_constraint() const -> const Physics_drag_constraint&
{
    return m_drag_constraint;
}

void Physics_tool::apply_drag_goal()
{
    glm::vec3 drag_point = m_goal_position_in_world;
    if (m_target_jointed) {
        // The spring holds the center of mass: its goal keeps the current
        // grab point -> center of mass offset.
        const glm::vec3 center_of_mass_in_world = glm::vec3{
            m_target_mesh->world_from_node() * glm::vec4{m_center_of_mass_in_node, 1.0f}
        };
        drag_point += center_of_mass_in_world - m_grab_position_world;
    }
    // A jointed body's spring drag point is teleported like the Transform
    // tool's: moved as kinematic, Jolt's MoveKinematic velocity carried the
    // drag point past a goal held still (measured: 1.2 m past it in 2 s).
    m_drag_constraint.move_drag_point(
        drag_point,
        m_target_jointed ? Drag_point_motion::teleport : Drag_point_motion::kinematic
    );

    // TODO investigate jolt damping
    if (!m_target_jointed && m_extra_damping_enable) {
        erhe::physics::IRigid_body* rigid_body = m_target_node_physics->get_rigid_body();
        glm::vec3 angular_velocity = rigid_body->get_angular_velocity();
        glm::vec3 linear_velocity  = rigid_body->get_linear_velocity();

        rigid_body->set_angular_velocity(angular_velocity * (1.0f - m_extra_angular_damping));
        rigid_body->set_linear_velocity(linear_velocity * (1.0f - m_extra_linear_damping));
    }
}

void Physics_tool::release_target()
{
    log_physics->trace("PT Target released");

    // Removes the constraint and the drag point body; the target keeps its
    // velocity and may sleep again.
    m_drag_constraint.detach();

    if (m_target_node_physics) {
        erhe::physics::IRigid_body* rigid_body = m_target_node_physics->get_rigid_body();
        if (m_overrides_applied && (rigid_body != nullptr)) {
            rigid_body->set_damping(m_original_linear_damping, m_original_angular_damping);
            rigid_body->set_friction(m_original_friction);
            rigid_body->set_gravity_factor(m_original_gravity);
        }
        m_target_node_physics.reset();
    }
    m_overrides_applied = false;
    m_target_jointed    = false;
    m_scripted_drag     = false;

    m_target_mesh.reset();

    m_target_distance                  = 1.0;
    m_grab_position_in_node            = glm::vec3{0.0, 0.0, 0.0};
    m_grab_position_in_collision_shape = glm::vec3{0.0, 0.0, 0.0};
    m_goal_position_in_world           = glm::vec3{0.0, 0.0, 0.0};
    m_to_end_direction                 = glm::vec3{0.0};
    m_to_start_direction               = glm::vec3{0.0};
    m_target_mesh_size                 = 0.0;
}

auto Physics_tool::get_last_target_mesh() const -> const std::shared_ptr<erhe::scene::Mesh>&
{
    return m_last_target_mesh;
}

void Physics_tool::on_items_removed(const Removed_items& removed)
{
    if (
        (m_target_mesh && removed.lookup.contains(m_target_mesh.get())) ||
        (m_target_node_physics && removed.lookup.contains(m_target_node_physics.get()))
    ) {
        release_target();
    }
    if (m_last_target_mesh && removed.lookup.contains(m_last_target_mesh.get())) {
        m_last_target_mesh.reset();
    }
}

void Physics_tool::on_close_scene(erhe::Item_host* const closing_host)
{
    if (m_target_mesh && (m_target_mesh->get_item_host() == closing_host)) {
        release_target();
    }
    if (m_last_target_mesh && (m_last_target_mesh->get_item_host() == closing_host)) {
        m_last_target_mesh.reset();
    }
}

auto Physics_tool::on_drag_ready() -> bool
{
    // When dynamic physics is disabled there is nothing to drag, so the tool must
    // not become ready: otherwise its right-mouse-drag binding (bound with
    // call_on_button_down_without_motion) consumes the button-down event and blocks
    // the camera turn / tumble that share the right mouse button. This mirrors the
    // guard in on_drag().
    if (!m_context.editor_settings->physics.dynamic_enable) {
        return false;
    }
    if (m_scripted_drag) {
        return false; // a scripted (MCP) drag holds the tool
    }

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
    m_hover_mesh = hover.scene_mesh_weak.lock();
}

auto Physics_tool::on_drag() -> bool
{
    if ((m_physics_world == nullptr) || m_scripted_drag) {
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
    if (!m_drag_constraint.is_attached()) {
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
        m_grab_position_world    = glm::vec3{m_target_mesh->world_from_node() * glm::vec4{m_grab_position_in_node, 1.0f}};
        m_goal_position_in_world = end.value();
    } else {
        m_grab_position_world = glm::vec3{m_target_mesh->world_from_node() * glm::vec4{m_grab_position_in_node, 1.0f}};

        erhe::math::Aabb mesh_bounding_box;
        for (const erhe::scene::Mesh_primitive& mesh_primitive : m_target_mesh->get_primitives()) {
            const erhe::primitive::Primitive& primitive              = *mesh_primitive.primitive.get();
            erhe::math::Aabb                  primitive_bounding_box = primitive.get_bounding_box();
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

    apply_drag_goal();
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

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    // TODO Make sure this has good enough perf, disable if not.
    if (m_target_mesh) {
        erhe::raytrace::IScene& rt_scene = context.scene_view.get_scene_root()->get_raytrace_scene();
        erhe::raytrace::Ray ray{
            .origin    = glm::vec3{m_target_mesh->position_in_world()},
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

    line_renderer.set_thickness(0.4f);
    if (m_drag_constraint.is_attached()) {
        const float d = 0.05f;
        const glm::vec3 dx{d, 0.0f, 0.0f};
        const glm::vec3 dy{0.0f, d, 0.0f};
        const glm::vec3 dz{0.0f, 0.0f, d};

        constexpr glm::vec4 white{6.0f, 1.5f, 1.5f, 1.0f};
        line_renderer.add_lines(
            white,
            { { m_grab_position_world, m_goal_position_in_world } }
        );
    }

    constexpr glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
    constexpr glm::vec3 axis_z{0.0f, 0.0f, 1.0f};

    if (m_show_drag_body && (m_drag_constraint.get_drag_point_body() != nullptr)) {
        const glm::mat4 m = m_drag_constraint.get_drag_point_body()->get_world_transform();
        const glm::vec4 half_red  {0.5f, 0.0f, 0.0f, 0.5f};
        const glm::vec4 half_green{0.0f, 0.5f, 0.0f, 0.5f};
        const glm::vec4 half_blue {0.0f, 0.0f, 0.5f, 0.5f};
        constexpr glm::vec3 O{ 0.0f };
        line_renderer.add_lines( m, half_red,   {{ O, axis_x }} );
        line_renderer.add_lines( m, half_green, {{ O, axis_y }} );
        line_renderer.add_lines( m, half_blue,  {{ O, axis_z }} );
    }
}

void Physics_tool::tool_properties(erhe::imgui::Imgui_window&)
{
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
    if (m_drag_constraint.is_attached()) {
        const glm::mat4 transform = m_drag_constraint.get_drag_point_body()->get_world_transform();
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
}

}
