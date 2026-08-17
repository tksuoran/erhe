#include "scene/frame_controller.hpp"

#include "editor_log.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/input_axis.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace editor {

using glm::mat4;
using glm::vec3;
using glm::vec4;

namespace {

constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

// Pitch around the camera's own X, yaw around world Y, roll around the camera's
// own Z - all three axes taken from the incoming orientation, matching the
// original matrix code. The single normalize at the end is what keeps the result
// a rotation: without it, every composition leaks a little scale into the basis
// and the next call amplifies it.
[[nodiscard]] auto compose_rotation(const glm::quat& orientation, const float rx, const float ry, const float rz) -> glm::quat
{
    const glm::vec3 axis_x = orientation * glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 axis_z = orientation * glm::vec3{0.0f, 0.0f, 1.0f};

    glm::quat result = orientation;
    if (rx != 0.0f) {
        result = glm::angleAxis(rx, axis_x) * result;
    }
    if (ry != 0.0f) {
        result = glm::angleAxis(ry, world_up) * result;
    }
    if (rz != 0.0f) {
        result = glm::angleAxis(rz, axis_z) * result;
    }
    return glm::normalize(result);
}

} // anonymous namespace

Frame_controller::Frame_controller()
    : Item{"frame controller"}
{
    reset();
    rotate_x      .set_damp     (0.700f);
    rotate_y      .set_damp     (0.700f);
    rotate_z      .set_damp     (0.700f);
    rotate_x      .set_max_delta(0.02f);
    rotate_y      .set_max_delta(0.02f);
    rotate_z      .set_max_delta(0.02f);
    translate_x   .set_damp     (0.92f);
    translate_y   .set_damp     (0.92f);
    translate_z   .set_damp     (0.92f);
    translate_x   .set_max_delta(0.004f);
    translate_y   .set_max_delta(0.004f);
    translate_z   .set_max_delta(0.004f);
    speed_modifier.set_max_value(3.0f);
    speed_modifier.set_damp     (0.92f);
    speed_modifier.set_max_delta(0.5f);
    update();
}

Frame_controller::Frame_controller(const Frame_controller&) = default;
Frame_controller& Frame_controller::operator=(const Frame_controller&) = default;
Frame_controller::~Frame_controller() noexcept = default;

auto Frame_controller::clone() const -> std::shared_ptr<erhe::Item_base>
{
    // It doesn't make sense copy Frame_controller - does it?
    return std::shared_ptr<erhe::Item_base>{};
}

auto Frame_controller::get_variable(const Variable control) -> erhe::math::Input_axis&
{
    switch (control) {
        case Variable::translate_x: return translate_x;
        case Variable::translate_y: return translate_y;
        case Variable::translate_z: return translate_z;
        case Variable::rotate_x   : return rotate_x;
        case Variable::rotate_y   : return rotate_y;
        case Variable::rotate_z   : return rotate_z;
        default: {
            ERHE_FATAL("bad control %04x", static_cast<unsigned int>(control));
        }
    }
}

void Frame_controller::set_position(const vec3 position)
{
    m_position = position;
    update();
}

void Frame_controller::set_orientation(const glm::quat& orientation)
{
    ERHE_CAMERA_ROLL_SCOPE(m_roll_monitor, "Frame_controller::set_orientation", m_orientation);
    m_orientation = glm::normalize(orientation);
    update();
}

void Frame_controller::set_orientation(const glm::mat4& orientation)
{
    // quat_cast does not project a non-orthonormal matrix onto the nearest
    // rotation, so normalize what comes back out of it.
    set_orientation(glm::quat_cast(orientation));
}

auto Frame_controller::get_position() const -> vec3
{
    return m_position;
}

auto Frame_controller::get_orientation() const -> glm::quat
{
    return m_orientation;
}

auto Frame_controller::get_orientation_matrix() const -> glm::mat4
{
    return glm::toMat4(m_orientation);
}

void Frame_controller::get_transform_from_node(erhe::scene::Node* node, const char* source)
{
    if (node == nullptr) {
        return;
    }
    // Readback: whatever the node's world transform says wins over the
    // controller's own orientation. This is where any transform applied by
    // someone else - a tool, a parent node, animation, undo, MCP - enters the
    // camera controller, and also where glm::decompose round-trip error enters,
    // so it is the most important site to attribute roll changes to.
    ERHE_CAMERA_ROLL_SCOPE(m_roll_monitor, source, m_orientation);
    static_cast<void>(source);
    const erhe::scene::Trs_transform& transform = node->world_from_node_transform();
    m_position = transform.get_translation();
    // Normalize: the node's rotation comes from glm::decompose, which does not
    // guarantee a unit quaternion, and any error here would otherwise be carried
    // straight back into the next rotation.
    m_orientation = glm::normalize(transform.get_rotation());
#if ERHE_CAMERA_ROLL_DIAGNOSTICS
    const glm::vec3 scale = transform.get_scale();
    const glm::vec3 skew  = transform.get_skew();
    const erhe::scene::Node* parent = node->get_parent_node().get();
    ERHE_CAMERA_ROLL_DETAIL(
        fmt::format(
            "node '{}', parent '{}', node scale ({}, {}, {}), node skew ({}, {}, {})",
            node->get_name(),
            (parent != nullptr) ? parent->get_name() : std::string{"(none)"},
            scale.x, scale.y, scale.z,
            skew.x, skew.y, skew.z
        )
    );
#endif
}

void Frame_controller::handle_node_update(erhe::scene::Node* old_node, erhe::scene::Node* new_node)
{
    static_cast<void>(old_node);
    if (new_node == nullptr) {
        return;
    }
    get_transform_from_node(new_node, "Frame_controller::handle_node_update (camera switch)");
#if ERHE_CAMERA_ROLL_DIAGNOSTICS
    // The camera changed: the previous camera's orientation is not a meaningful
    // baseline for roll attribution, so adopt the new one without reporting.
    m_roll_monitor.rebase(measure_camera_orientation(m_orientation));
#endif
}

void Frame_controller::handle_node_transform_update()
{
    if (m_transform_update) {
        return;
    }

    auto* node = get_node();
    if (node == nullptr) {
        return;
    }
    // Reached only for transform writes the controller did not make itself
    // (m_transform_update guards its own writes) and for the deferred
    // propagation pass in Scene::update_node_transforms().
    get_transform_from_node(node, "external node transform write (Frame_controller::handle_node_transform_update)");
    update();
}

void Frame_controller::reset()
{
    translate_x.reset();
    translate_y.reset();
    translate_z.reset();
    rotate_x.reset();
    rotate_y.reset();
    rotate_z.reset();
}

void Frame_controller::update()
{
    auto* node = get_node();
    if (node == nullptr) {
        return;
    }

#if ERHE_CAMERA_ROLL_DIAGNOSTICS
    if (m_roll_monitor.enabled) {
        const Roll_measurement measurement = measure_camera_orientation(m_orientation);
        if (measurement.orthonormality_error > m_orthonormality_warn_threshold) {
            log_camera_roll->warn(
                "Frame_controller orientation basis drifted: orthonormality error {:.3e}, determinant {:.6f}, roll {:.6f} deg (valid {})",
                measurement.orthonormality_error,
                measurement.determinant,
                glm::degrees(measurement.roll_radians),
                measurement.roll_valid
            );
            // Geometric backoff: report again only once the drift has doubled.
            m_orthonormality_warn_threshold = measurement.orthonormality_error * 2.0f;
        }
    }
#endif

    m_transform_update = true;
    node->set_world_from_node(erhe::scene::Trs_transform{m_position, m_orientation});
    const glm::vec4 direction_in_world = node->direction_in_world();
    // log_input_frame->info("Frame_controller::update() direction_in_world = {}", glm::vec3{direction_in_world});
    m_transform_update = false;
}

// The axes are unit vectors as long as m_orientation is a unit quaternion, which
// it is by construction. apply_rotation() feeds them to create_rotation(), which
// applies Rodrigues with the axis as given and only produces a rotation for a
// unit axis - that is what made a non-unit basis self-amplifying before.
auto Frame_controller::get_axis_x() const -> vec3
{
    return m_orientation * vec3{1.0f, 0.0f, 0.0f};
}

auto Frame_controller::get_axis_y() const -> vec3
{
    return m_orientation * vec3{0.0f, 1.0f, 0.0f};
}

auto Frame_controller::get_axis_z() const -> vec3
{
    return m_orientation * vec3{0.0f, 0.0f, 1.0f};
}

void Frame_controller::set_active_control_value(const Variable variable, float value)
{
    switch (variable) {
        case Variable::translate_x: active_translate_x = value; break;
        case Variable::translate_y: active_translate_y = value; break;
        case Variable::translate_z: active_translate_z = value; break;
        case Variable::rotate_x:    active_rotate_x = value; break;
        case Variable::rotate_y:    active_rotate_y = value; break;
        case Variable::rotate_z:    active_rotate_z = value; break;
        default: break;
    }
}

auto Frame_controller::get_active_control_value(const Variable variable) const -> float
{
    switch (variable) {
        case Variable::translate_x: return active_translate_x;
        case Variable::translate_y: return active_translate_y;
        case Variable::translate_z: return active_translate_z;
        case Variable::rotate_x:    return active_rotate_x;
        case Variable::rotate_y:    return active_rotate_y;
        case Variable::rotate_z:    return active_rotate_z;
        default: return 0.0f;
    }
}

void Frame_controller::update_fixed_step()
{
    ERHE_CAMERA_ROLL_SCOPE(m_roll_monitor, "Frame_controller::update_fixed_step", m_orientation);

    // TODO Only do once until next update()
    get_transform_from_node(get_node(), "Frame_controller::update_fixed_step readback (node -> controller)");

    translate_x   .update();
    translate_y   .update();
    translate_z   .update();
    rotate_x      .update();
    rotate_y      .update();
    rotate_z      .update();
    speed_modifier.update();

    const float speed = move_speed + speed_modifier.current_value();
    float tx = translate_x.current_value() + active_translate_x;
    if (tx != 0.0f) {
        m_position += get_axis_x() * tx * speed;
    }

    float ty = translate_y.current_value() + active_translate_y;
    if (ty != 0.0f) {
        m_position += get_axis_y() * ty * speed;
    }

    float tz = translate_z.current_value() + active_translate_z;
    if (tz != 0.0f) {
        m_position += get_axis_z() * tz * speed;
    }

    float rx = rotate_x.current_value() + active_rotate_x;
    float ry = rotate_y.current_value() + active_rotate_y;
    apply_rotation(rx, ry, 0.0f);

    update();
}

void Frame_controller::apply_rotation(float rx, float ry, float rz)
{
    // log_input_frame->info("Frame_controller::apply_rotation() rx = {}, ry = {}, rz = {}", rx, ry, rz);
    ERHE_CAMERA_ROLL_SCOPE(m_roll_monitor, "Frame_controller::apply_rotation", m_orientation);
#if ERHE_CAMERA_ROLL_DIAGNOSTICS
    const glm::vec3 detail_axis_x = get_axis_x();
    ERHE_CAMERA_ROLL_DETAIL(
        fmt::format(
            "rx {} (around local X ({}, {}, {})), ry {} (around world Y), rz {} (around local Z)",
            rx, detail_axis_x.x, detail_axis_x.y, detail_axis_x.z, ry, rz
        )
    );
#endif

    m_orientation = compose_rotation(m_orientation, rx, ry, rz);
    update();
}

#if ERHE_CAMERA_ROLL_DIAGNOSTICS
void Frame_controller::level_roll()
{
    const Roll_measurement measurement = measure_camera_orientation(m_orientation);
    if (!measurement.roll_valid) {
        log_camera_roll->warn("Cannot level camera roll: camera looks (near) straight up or down, roll is not measurable");
        return;
    }
    if (measurement.roll_radians == 0.0f) {
        return;
    }

    ERHE_CAMERA_ROLL_SCOPE(m_roll_monitor, "Frame_controller::level_roll", m_orientation);
    ERHE_CAMERA_ROLL_DETAIL(fmt::format("removing {:.6f} deg of roll", glm::degrees(measurement.roll_radians)));

    // Rebuild the orientation from the current view direction and world up.
    const glm::vec3 back  = get_axis_z();
    const glm::vec3 right = glm::normalize(glm::cross(world_up, back));
    const glm::vec3 up    = glm::cross(back, right);
    m_orientation = glm::normalize(glm::quat_cast(glm::mat3{right, up, back}));
    m_orthonormality_warn_threshold = 1.0e-5f;
    update();
}
#endif // ERHE_CAMERA_ROLL_DIAGNOSTICS

void Frame_controller::apply_tumble(glm::vec3 pivot, float rx, float ry, float rz)
{
    ERHE_CAMERA_ROLL_SCOPE(m_roll_monitor, "Frame_controller::apply_tumble", m_orientation);
    ERHE_CAMERA_ROLL_DETAIL(fmt::format("pivot ({}, {}, {}), rx {}, ry {}, rz {}", pivot.x, pivot.y, pivot.z, rx, ry, rz));

    const glm::quat new_orientation = compose_rotation(m_orientation, rx, ry, rz);
    {
        // Orbit the position around the pivot by the same rotation. The old code
        // used transpose(orientation) as the inverse, which is only correct for
        // an orthonormal basis; conjugate() is exact for a unit quaternion.
        const glm::vec3 direction_in_world = m_position - pivot;
        const glm::vec3 direction_in_view  = glm::conjugate(m_orientation) * direction_in_world;
        m_position = pivot + (new_orientation * direction_in_view);
    }
    m_orientation = new_orientation;
    update();
}

}
