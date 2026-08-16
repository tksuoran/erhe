#include "xr/controller_visualization.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_geometry/shapes/torus.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_xr/xr_action.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_xr/xr_session.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>

using erhe::geometry::transform_mesh;
using erhe::geometry::to_geo_mat4f;

namespace editor {

namespace {

// The runtime controller GLB carries one animation, "All Animations", a
// 24 fps timeline where specific frames hold each control's actuated pose
// (verified offline by dumping the channels of both hands' GLBs; both use
// the same layout):
//
//   frame  0 - every control neutral
//   frame  5 - A / X button fully pressed
//   frame  8 - B / Y button fully pressed
//   frame 16 - trigger fully pulled
//   frame 21 - grip trigger fully squeezed
//   frame 24 - oculus button pressed (not driven - not exposed to apps)
//   frame 29 / 31 - thumbstick tilted fully forward / back  (stick +y / -y)
//   frame 34 / 32 - thumbstick tilted fully right / left    (stick +x / -x)
//
// The values ramp only across the one or two frames next to each pose, so
// input cannot be mapped to animation *time*; instead the poses are sampled
// once at load and blended per frame by the input value.
constexpr int   frame_primary_button_pressed  {5};
constexpr int   frame_secondary_button_pressed{8};
constexpr int   frame_trigger_pulled          {16};
constexpr int   frame_grip_squeezed           {21};
constexpr int   frame_thumbstick_forward      {29};
constexpr int   frame_thumbstick_back         {31};
constexpr int   frame_thumbstick_left         {32};
constexpr int   frame_thumbstick_right        {34};
constexpr float frames_per_second             {24.0f};

[[nodiscard]] auto key_time(const int frame) -> float
{
    return static_cast<float>(frame) / frames_per_second;
}

class Joint_sample
{
public:
    bool      ok         {false};
    glm::vec3 translation{0.0f};
    glm::quat rotation   {1.0f, 0.0f, 0.0f, 0.0f};
};

[[nodiscard]] auto sample_joint(erhe::scene::Animation& animation, const std::string_view joint_name, const float time) -> Joint_sample
{
    Joint_sample result{};
    for (erhe::scene::Animation_channel& channel : animation.channels) {
        if (!channel.target || (channel.target->get_name() != joint_name)) {
            continue;
        }
        const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
        const glm::vec4 value = sampler.evaluate(channel, time);
        if (channel.path == erhe::scene::Animation_path::TRANSLATION) {
            result.translation = glm::vec3{value};
            result.ok = true;
        } else if (channel.path == erhe::scene::Animation_path::ROTATION) {
            result.rotation = glm::quat{value.w, value.x, value.y, value.z};
            result.ok = true;
        }
    }
    return result;
}

[[nodiscard]] auto find_joint_node(erhe::scene::Animation& animation, const std::string_view joint_name) -> std::shared_ptr<erhe::scene::Node>
{
    for (const erhe::scene::Animation_channel& channel : animation.channels) {
        if (channel.target && (channel.target->get_name() == joint_name)) {
            return channel.target;
        }
    }
    return {};
}

// Rotation vector (axis * angle, in the joint's parent space) carrying the
// neutral rotation onto the actuated one: actuated = exp(v) * neutral.
[[nodiscard]] auto rotation_vector_between(const glm::quat& neutral, const glm::quat& actuated) -> glm::vec3
{
    glm::quat relative = actuated * glm::inverse(neutral);
    if (relative.w < 0.0f) {
        relative = -relative;
    }
    const float angle = glm::angle(relative);
    if (angle < 1.0e-6f) {
        return glm::vec3{0.0f};
    }
    return glm::axis(relative) * angle;
}

} // anonymous namespace

Controller_visualization::Controller_visualization(
    erhe::scene::Node*                 view_root,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    Scene_root&                        scene_root
)
    : m_mesh_memory{mesh_memory}
{
    ERHE_PROFILE_FUNCTION();

    m_material_library = scene_root.get_content_library()->materials;
    m_content_layer_id = scene_root.layers().content()->id;

    auto controller_material = m_material_library->make<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "Controller",
            .data = {
                .base_color = glm::vec3{0.1f, 0.1f, 0.2f}
            }
        }
    );

    GEO::Mesh controller_geo_mesh{3, true};
    erhe::geometry::shapes::make_torus(controller_geo_mesh, 0.05f, 0.0025f, 40, 14);
    transform_mesh(controller_geo_mesh, to_geo_mat4f(erhe::math::mat4_swap_yz));

    erhe::primitive::Element_mappings dummy{};
    erhe::primitive::Buffer_mesh buffer_mesh{};
    const bool buffer_mesh_ok = erhe::primitive::build_buffer_mesh(
        buffer_mesh,
        controller_geo_mesh,
        erhe::primitive::Build_info{
            .primitive_types = {.fill_triangles = true },
            .buffer_info = mesh_memory.make_primitive_buffer_info()
        },
        dummy, // TODO make element mappings optional
        erhe::primitive::Normal_style::corner_normals
    );
    ERHE_VERIFY(buffer_mesh_ok); // TODO handle possible error (out of memory)

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(std::move(buffer_mesh));

    for (const bool right_hand : { false, true }) {
        Hand& hand = get_hand(right_hand);
        const char* const node_name = right_hand ? "Controller node right" : "Controller node left";
        const char* const mesh_name = right_hand ? "Controller right"      : "Controller left";
        hand.node             = std::make_shared<erhe::scene::Node>(node_name);
        hand.placeholder_mesh = std::make_shared<erhe::scene::Mesh>(mesh_name);
        hand.placeholder_mesh->add_primitive(primitive, controller_material);
        hand.node->enable_flag_bits(erhe::Item_flags::visible);
        hand.placeholder_mesh->enable_flag_bits(erhe::Item_flags::controller);
        hand.placeholder_mesh->layer_id = m_content_layer_id;
        hand.node->attach(hand.placeholder_mesh);
        hand.node->set_parent(view_root);
    }
}

auto Controller_visualization::get_hand(const bool right_hand) -> Hand&
{
    return right_hand ? m_right_hand : m_left_hand;
}

void Controller_visualization::load_render_models(App_context& context, erhe::xr::Xr_session& xr_session)
{
    load_render_model(context, xr_session, false);
    load_render_model(context, xr_session, true);
}

void Controller_visualization::load_render_model(App_context& context, erhe::xr::Xr_session& xr_session, const bool right_hand)
{
    ERHE_PROFILE_FUNCTION();

    Hand& hand = get_hand(right_hand);
    if (hand.has_render_model) {
        return;
    }
    if ((context.graphics_device == nullptr) || (context.current_command_buffer == nullptr) || (context.executor == nullptr)) {
        return;
    }

    erhe::xr::Render_model_data model = xr_session.load_controller_render_model(right_hand);
    if (model.data.empty()) {
        log_xr->info("No controller render model for {} hand - keeping torus placeholder", right_hand ? "right" : "left");
        return;
    }

    // Parse the GLB under a detached node; attach only on success.
    std::shared_ptr<erhe::scene::Node> model_root = std::make_shared<erhe::scene::Node>(
        right_hand ? "Controller model right" : "Controller model left"
    );
    model_root->enable_flag_bits(erhe::Item_flags::visible);

    erhe::gltf::Image_transfer image_transfer{*context.graphics_device};
    const erhe::gltf::Gltf_parse_arguments parse_arguments{
        .graphics_device = *context.graphics_device,
        .executor        = *context.executor,
        .image_transfer  = image_transfer,
        .root_node       = model_root,
        .mesh_layer_id   = m_content_layer_id,
        .path            = std::filesystem::path{model.name + ".glb"},
        .parallel        = false,
        .glb_data        = std::span<const std::byte>{reinterpret_cast<const std::byte*>(model.data.data()), model.data.size()}
    };
    const erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(parse_arguments);

    const erhe::primitive::Build_info build_info{
        .primitive_types = { .fill_triangles = true },
        .buffer_info     = m_mesh_memory.make_primitive_buffer_info()
    };
    // The controller meshes are skinned: buttons / triggers / thumbstick are
    // bones ("skeleton #1", one joint per control), and the root joint
    // carries a basis-change rotation that only the joint-matrix path
    // applies. Rendering them unskinned leaves those vertex clusters
    // untransformed (visible as spikes), so skinned meshes get the skinned
    // vertex format (joint indices + weights -> USE_SKINNING), mirroring
    // finalize_imported_meshes().
    const erhe::primitive::Build_info skinned_build_info{
        .primitive_types = { .fill_triangles = true },
        .buffer_info     = m_mesh_memory.make_skinned_primitive_buffer_info()
    };
    std::size_t renderable_primitive_count = 0;
    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        node->enable_flag_bits(erhe::Item_flags::visible);
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (!mesh) {
            continue;
        }
        // The battery level indicator quad expects the app to window its UVs
        // to one cell of a four-cell battery-level atlas, but controller
        // battery level is not queryable by third-party apps on Quest (see
        // doc/xr-controller-render-model.md), so hide the quad entirely.
        if (node->get_name().ends_with("batteryIndicatorQuad")) {
            node->disable_flag_bits(erhe::Item_flags::visible);
            mesh->disable_flag_bits(erhe::Item_flags::visible);
            continue;
        }
        mesh->enable_flag_bits(erhe::Item_flags::controller);
        mesh->layer_id = m_content_layer_id;
        // Renderable fill triangles only: no edges, and deliberately no
        // raytrace - the controller ray originates next to the model and
        // must never hover-pick the controller itself.
        const erhe::primitive::Build_info& mesh_build_info = mesh->skin ? skinned_build_info : build_info;
        for (erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_mutable_primitives()) {
            const bool renderable_ok = mesh_primitive.primitive->make_renderable_mesh(mesh_build_info, erhe::primitive::Normal_style::corner_normals);
            if (renderable_ok) {
                ++renderable_primitive_count;
            } else {
                log_xr->warn("Controller render model '{}': failed to build renderable mesh", model.name);
            }
        }
    }
    if (renderable_primitive_count == 0) {
        log_xr->warn("Controller render model '{}': no renderable meshes - keeping torus placeholder", model.name);
        return;
    }

    for (const std::shared_ptr<erhe::primitive::Material>& material : gltf_data.materials) {
        if (material) {
            m_material_library->add(material);
        }
    }

    model_root->set_parent(hand.node.get());
    hand.node->detach(hand.placeholder_mesh.get());
    hand.has_render_model = true;
    if (!gltf_data.animations.empty() && gltf_data.animations.front()) {
        setup_control_drives(hand, *gltf_data.animations.front(), right_hand);
    } else {
        log_xr->warn("Controller render model '{}': no animation - control joints stay static", model.name);
    }
    log_xr->info(
        "Controller render model '{}' loaded for {} hand: {} renderable primitives",
        model.name,
        right_hand ? "right" : "left",
        renderable_primitive_count
    );
}

void Controller_visualization::setup_control_drives(Hand& hand, erhe::scene::Animation& animation, const bool right_hand)
{
    const auto make_drive = [&animation](const std::string_view joint_name, const int actuated_frame) -> Control_drive {
        Control_drive drive{};
        drive.node = find_joint_node(animation, joint_name);
        if (!drive.node) {
            log_xr->warn("Controller render model: no animation channels for joint '{}'", joint_name);
            return drive;
        }
        const Joint_sample neutral  = sample_joint(animation, joint_name, key_time(0));
        const Joint_sample actuated = sample_joint(animation, joint_name, key_time(actuated_frame));
        drive.neutral_translation  = neutral.translation;
        drive.neutral_rotation     = neutral.rotation;
        drive.actuated_translation = actuated.translation;
        drive.actuated_rotation    = actuated.rotation;
        return drive;
    };

    hand.primary_button   = make_drive(right_hand ? "b_button_a" : "b_button_x", frame_primary_button_pressed);
    hand.secondary_button = make_drive(right_hand ? "b_button_b" : "b_button_y", frame_secondary_button_pressed);
    hand.trigger          = make_drive("b_trigger_front", frame_trigger_pulled);
    hand.grip_trigger     = make_drive("b_trigger_grip",  frame_grip_squeezed);

    hand.thumbstick = Thumbstick_drive{};
    hand.thumbstick.node = find_joint_node(animation, "b_thumbstick");
    if (hand.thumbstick.node) {
        const Joint_sample neutral = sample_joint(animation, "b_thumbstick", key_time(0));
        hand.thumbstick.translation      = neutral.translation;
        hand.thumbstick.neutral_rotation = neutral.rotation;
        hand.thumbstick.tilt_x_positive  = rotation_vector_between(neutral.rotation, sample_joint(animation, "b_thumbstick", key_time(frame_thumbstick_right  )).rotation);
        hand.thumbstick.tilt_x_negative  = rotation_vector_between(neutral.rotation, sample_joint(animation, "b_thumbstick", key_time(frame_thumbstick_left   )).rotation);
        hand.thumbstick.tilt_y_positive  = rotation_vector_between(neutral.rotation, sample_joint(animation, "b_thumbstick", key_time(frame_thumbstick_forward)).rotation);
        hand.thumbstick.tilt_y_negative  = rotation_vector_between(neutral.rotation, sample_joint(animation, "b_thumbstick", key_time(frame_thumbstick_back   )).rotation);
    } else {
        log_xr->warn("Controller render model: no animation channels for joint 'b_thumbstick'");
    }
}

void Controller_visualization::update_hand_controls(const bool right_hand, const erhe::xr::Xr_actions* actions)
{
    Hand& hand = get_hand(right_hand);
    if (!hand.has_render_model || (actions == nullptr)) {
        return;
    }

    const auto boolean_weight = [](const erhe::xr::Xr_action_boolean* action) -> float {
        const bool pressed = (action != nullptr) && (action->state.isActive == XR_TRUE) && (action->state.currentState == XR_TRUE);
        return pressed ? 1.0f : 0.0f;
    };
    const auto float_weight = [](const erhe::xr::Xr_action_float* action) -> float {
        const bool active = (action != nullptr) && (action->state.isActive == XR_TRUE);
        return active ? std::clamp(action->state.currentState, 0.0f, 1.0f) : 0.0f;
    };
    const auto apply_drive = [](const Control_drive& drive, const float weight) {
        if (!drive.node) {
            return;
        }
        erhe::scene::Trs_transform transform = drive.node->parent_from_node_transform();
        transform.set_translation_and_rotation(
            glm::mix  (drive.neutral_translation, drive.actuated_translation, weight),
            glm::slerp(drive.neutral_rotation,    drive.actuated_rotation,    weight)
        );
        drive.node->set_parent_from_node(transform);
    };

    apply_drive(hand.primary_button,   boolean_weight(right_hand ? actions->a_click : actions->x_click));
    apply_drive(hand.secondary_button, boolean_weight(right_hand ? actions->b_click : actions->y_click));
    apply_drive(hand.trigger,          float_weight(actions->trigger_value));
    apply_drive(hand.grip_trigger,     float_weight(actions->squeeze_value));

    const Thumbstick_drive& thumbstick = hand.thumbstick;
    if (thumbstick.node != nullptr) {
        glm::vec2 stick{0.0f};
        if ((actions->thumbstick != nullptr) && (actions->thumbstick->state.isActive == XR_TRUE)) {
            stick.x = std::clamp(actions->thumbstick->state.currentState.x, -1.0f, 1.0f);
            stick.y = std::clamp(actions->thumbstick->state.currentState.y, -1.0f, 1.0f);
        }
        const glm::vec3 tilt =
            ((stick.x >= 0.0f) ? (stick.x * thumbstick.tilt_x_positive) : (-stick.x * thumbstick.tilt_x_negative)) +
            ((stick.y >= 0.0f) ? (stick.y * thumbstick.tilt_y_positive) : (-stick.y * thumbstick.tilt_y_negative));
        const float angle = glm::length(tilt);
        const glm::quat deflection = (angle > 1.0e-6f)
            ? glm::angleAxis(angle, tilt / angle)
            : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        erhe::scene::Trs_transform transform = thumbstick.node->parent_from_node_transform();
        transform.set_translation_and_rotation(thumbstick.translation, deflection * thumbstick.neutral_rotation);
        thumbstick.node->set_parent_from_node(transform);
    }
}

void Controller_visualization::update_hand(
    const bool                      right_hand,
    const erhe::xr::Xr_action_pose* grip_pose,
    const erhe::xr::Xr_action_pose* aim_pose,
    const glm::vec3&                camera_offset
)
{
    Hand& hand = get_hand(right_hand);
    if (!hand.node) {
        return;
    }
    // The render model origin is defined at the grip pose; the torus
    // placeholder follows the aim pose (its historical behavior).
    const erhe::xr::Xr_action_pose* pose = hand.has_render_model ? grip_pose : aim_pose;
    const bool tracked = (pose != nullptr) && (pose->location.locationFlags != 0);
    if (!tracked) {
        hand.node->disable_flag_bits(erhe::Item_flags::visible);
        return;
    }
    hand.node->enable_flag_bits(erhe::Item_flags::visible);
    const glm::mat4 orientation = glm::mat4_cast(pose->orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{1}, pose->position + camera_offset);
    const glm::mat4 m           = translation * orientation;
    hand.node->set_parent_from_node(m);
}

}
