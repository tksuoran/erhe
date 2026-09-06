#include "erhe_scene/xform_op.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <array>
#include <cmath>

namespace erhe::scene {

namespace {

template <typename T>
[[nodiscard]] auto value_as(const Xform_op_value& value) -> T
{
    const T* const typed = std::get_if<T>(&value);
    ERHE_VERIFY(typed != nullptr);
    return *typed;
}

[[nodiscard]] auto rotate_about_axis(const int axis, const double angle_degrees) -> glm::dmat4
{
    glm::dvec3 axis_vector{0.0, 0.0, 0.0};
    axis_vector[axis] = 1.0;
    return glm::rotate(glm::dmat4{1.0}, glm::radians(angle_degrees), axis_vector);
}

// The axis order of a three-angle rotate op, first applied axis first
// (0 = x, 1 = y, 2 = z).
class Euler_order
{
public:
    int i{0};
    int j{1};
    int k{2};
    // +1 when (i, j, k) is an even permutation of (0, 1, 2), -1 when odd.
    int parity{1};
};

[[nodiscard]] auto get_euler_order(const Xform_op_type type) -> Euler_order
{
    switch (type) {
        case Xform_op_type::rotate_xyz: return Euler_order{0, 1, 2,  1};
        case Xform_op_type::rotate_xzy: return Euler_order{0, 2, 1, -1};
        case Xform_op_type::rotate_yxz: return Euler_order{1, 0, 2, -1};
        case Xform_op_type::rotate_yzx: return Euler_order{1, 2, 0,  1};
        case Xform_op_type::rotate_zxy: return Euler_order{2, 0, 1,  1};
        case Xform_op_type::rotate_zyx: return Euler_order{2, 1, 0, -1};
        default: {
            ERHE_FATAL("not a three-angle rotate op");
        }
    }
}

[[nodiscard]] auto is_three_angle_rotate(const Xform_op_type type) -> bool
{
    switch (type) {
        case Xform_op_type::rotate_xyz:
        case Xform_op_type::rotate_xzy:
        case Xform_op_type::rotate_yxz:
        case Xform_op_type::rotate_yzx:
        case Xform_op_type::rotate_zxy:
        case Xform_op_type::rotate_zyx: return true;
        default: return false;
    }
}

[[nodiscard]] auto single_axis_of(const Xform_op_type type) -> int
{
    switch (type) {
        case Xform_op_type::rotate_x: return 0;
        case Xform_op_type::rotate_y: return 1;
        case Xform_op_type::rotate_z: return 2;
        default: return -1;
    }
}

[[nodiscard]] auto is_rotate_op(const Xform_op_type type) -> bool
{
    return (type == Xform_op_type::orient) || (single_axis_of(type) >= 0) || is_three_angle_rotate(type);
}

// The angle in degrees of a rotation about `axis`, when the rotation is about
// that axis. Returns false when it is not.
[[nodiscard]] auto angle_about_axis(const glm::dquat rotation, const int axis, double& out_angle_degrees) -> bool
{
    constexpr double tolerance = 1e-5;
    glm::dquat q = glm::normalize(rotation);
    if (q.w < 0.0) {
        q = -q; // Same rotation, canonical hemisphere
    }
    const std::array<double, 3> imaginary{q.x, q.y, q.z};
    for (int other = 0; other < 3; ++other) {
        if (other == axis) {
            continue;
        }
        if (std::abs(imaginary[static_cast<std::size_t>(other)]) > tolerance) {
            return false;
        }
    }
    out_angle_degrees = glm::degrees(2.0 * std::atan2(imaginary[static_cast<std::size_t>(axis)], q.w));
    return true;
}

[[nodiscard]] auto trs_matrix(const glm::vec3 translation, const glm::quat rotation, const glm::vec3 scale) -> glm::dmat4
{
    const glm::dmat4 t = glm::translate(glm::dmat4{1.0}, glm::dvec3{translation});
    const glm::dmat4 r = glm::mat4_cast(glm::dquat{rotation});
    const glm::dmat4 s = glm::scale(glm::dmat4{1.0}, glm::dvec3{scale});
    return t * r * s;
}

[[nodiscard]] auto is_near(const glm::vec3 lhs, const glm::vec3 rhs) -> bool
{
    constexpr float tolerance = 1e-5f;
    return (std::abs(lhs.x - rhs.x) < tolerance) &&
           (std::abs(lhs.y - rhs.y) < tolerance) &&
           (std::abs(lhs.z - rhs.z) < tolerance);
}

[[nodiscard]] auto is_near(const glm::dmat4& lhs, const glm::dmat4& rhs) -> bool
{
    constexpr double tolerance = 1e-5;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(lhs[column][row] - rhs[column][row]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto is_near(const glm::quat lhs, const glm::quat rhs) -> bool
{
    constexpr float tolerance = 1e-6f;
    return std::abs(std::abs(glm::dot(lhs, rhs)) - 1.0f) < tolerance;
}

} // anonymous namespace

auto euler_matrix(const Xform_op_type type, const glm::dvec3 angles_degrees) -> glm::dmat4
{
    const Euler_order order = get_euler_order(type);
    // The op applies its axes i, then j, then k to a point, so the
    // column-vector matrix product runs the other way around.
    return
        rotate_about_axis(order.k, angles_degrees[order.k]) *
        rotate_about_axis(order.j, angles_degrees[order.j]) *
        rotate_about_axis(order.i, angles_degrees[order.i]);
}

auto euler_degrees_from_rotation(const Xform_op_type type, const glm::dquat rotation) -> glm::dvec3
{
    const Euler_order order = get_euler_order(type);
    const glm::dmat4  m     = glm::mat4_cast(glm::normalize(rotation));
    const double      e     = static_cast<double>(order.parity);

    const double sin_b = glm::clamp(-e * m[order.i][order.k], -1.0, 1.0);
    const double b     = std::asin(sin_b);
    const double cos_b = std::cos(b);

    double a{0.0};
    double c{0.0};
    constexpr double gimbal_lock_tolerance = 1e-7;
    if (std::abs(cos_b) > gimbal_lock_tolerance) {
        a = std::atan2(e * m[order.j][order.k], m[order.k][order.k]);
        c = std::atan2(e * m[order.i][order.j], m[order.i][order.i]);
    } else {
        // Gimbal lock: only the sum (or difference) of a and c is determined.
        // Put all of it in c.
        a = 0.0;
        c = std::atan2(-e * m[order.j][order.i], m[order.j][order.j]);
    }

    glm::dvec3 angles_degrees{0.0};
    angles_degrees[order.i] = glm::degrees(a);
    angles_degrees[order.j] = glm::degrees(b);
    angles_degrees[order.k] = glm::degrees(c);
    return angles_degrees;
}

auto Xform_op::to_matrix() const -> glm::dmat4
{
    glm::dmat4 matrix{1.0};
    switch (type) {
        case Xform_op_type::translate: {
            matrix = glm::translate(glm::dmat4{1.0}, value_as<glm::dvec3>(value));
            break;
        }
        case Xform_op_type::scale: {
            matrix = glm::scale(glm::dmat4{1.0}, value_as<glm::dvec3>(value));
            break;
        }
        case Xform_op_type::rotate_x:
        case Xform_op_type::rotate_y:
        case Xform_op_type::rotate_z: {
            matrix = rotate_about_axis(single_axis_of(type), value_as<double>(value));
            break;
        }
        case Xform_op_type::rotate_xyz:
        case Xform_op_type::rotate_xzy:
        case Xform_op_type::rotate_yxz:
        case Xform_op_type::rotate_yzx:
        case Xform_op_type::rotate_zxy:
        case Xform_op_type::rotate_zyx: {
            matrix = euler_matrix(type, value_as<glm::dvec3>(value));
            break;
        }
        case Xform_op_type::orient: {
            matrix = glm::mat4_cast(glm::normalize(value_as<glm::dquat>(value)));
            break;
        }
        case Xform_op_type::transform: {
            matrix = value_as<glm::dmat4>(value);
            break;
        }
        default: {
            ERHE_FATAL("unknown xformOp type");
        }
    }
    return inverted ? glm::inverse(matrix) : matrix;
}

auto Xform_op::operator==(const Xform_op& other) const -> bool
{
    return (type      == other.type    ) &&
           (precision == other.precision) &&
           (suffix    == other.suffix  ) &&
           (inverted  == other.inverted) &&
           (value     == other.value   );
}

auto Xform_op::operator!=(const Xform_op& other) const -> bool
{
    return !(*this == other);
}

auto Xform_op_stack::compose() const -> glm::dmat4
{
    glm::dmat4 matrix{1.0};
    for (const Xform_op& op : ops) {
        matrix = matrix * op.to_matrix();
    }
    return matrix;
}

auto Xform_op_stack::operator==(const Xform_op_stack& other) const -> bool
{
    return (reset_xform_stack == other.reset_xform_stack) && (ops == other.ops);
}

auto Xform_op_stack::operator!=(const Xform_op_stack& other) const -> bool
{
    return !(*this == other);
}

auto write_trs_into_xform_op_stack(
    Xform_op_stack&  stack,
    const glm::vec3  translation,
    const glm::quat  rotation,
    const glm::vec3  scale
) -> Xform_op_write_back_result
{
    // A stack that is one plain `transform` op carries any transform.
    if (
        (stack.ops.size() == 1) &&
        (stack.ops.front().type == Xform_op_type::transform) &&
        !stack.ops.front().inverted &&
        stack.ops.front().suffix.empty()
    ) {
        stack.ops.front().value = trs_matrix(translation, rotation, scale);
        return Xform_op_write_back_result::written;
    }

    Xform_op* translate_op{nullptr};
    Xform_op* rotate_op   {nullptr};
    Xform_op* scale_op    {nullptr};
    for (Xform_op& op : stack.ops) {
        if (op.inverted || !op.suffix.empty()) {
            continue;
        }
        if (op.type == Xform_op_type::translate) {
            translate_op = &op;
        } else if (op.type == Xform_op_type::scale) {
            scale_op = &op;
        } else if (is_rotate_op(op.type)) {
            rotate_op = &op;
        }
    }

    // What the stack composes to today decides which components the edit
    // actually changed.
    glm::vec3 current_translation{0.0f};
    glm::quat current_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 current_scale      {1.0f};
    {
        glm::vec3 skew{0.0f};
        glm::vec4 perspective{0.0f};
        const glm::mat4 current = glm::mat4{stack.compose()};
        glm::decompose(current, current_scale, current_rotation, current_translation, skew, perspective);
    }

    const bool translation_changed = !is_near(current_translation, translation);
    const bool rotation_changed    = !is_near(current_rotation,    rotation   );
    const bool scale_changed       = !is_near(current_scale,       scale      );

    if (
        (translation_changed && (translate_op == nullptr)) ||
        (rotation_changed    && (rotate_op    == nullptr)) ||
        (scale_changed       && (scale_op     == nullptr))
    ) {
        return Xform_op_write_back_result::not_representable;
    }

    // The rotation conversion can fail (a single-axis op and a rotation that
    // is not about that axis), so convert before anything is written.
    Xform_op_value new_rotation_value;
    if (rotation_changed) {
        const glm::dquat rotation_d{rotation};
        const int        single_axis = single_axis_of(rotate_op->type);
        if (rotate_op->type == Xform_op_type::orient) {
            new_rotation_value = rotation_d;
        } else if (single_axis >= 0) {
            double angle_degrees{0.0};
            if (!angle_about_axis(rotation_d, single_axis, angle_degrees)) {
                return Xform_op_write_back_result::not_representable;
            }
            new_rotation_value = angle_degrees;
        } else {
            new_rotation_value = euler_degrees_from_rotation(rotate_op->type, rotation_d);
        }
    }

    if (!translation_changed && !rotation_changed && !scale_changed) {
        return Xform_op_write_back_result::written;
    }

    const Xform_op_value old_translation_value = (translate_op != nullptr) ? translate_op->value : Xform_op_value{};
    const Xform_op_value old_rotation_value    = (rotate_op    != nullptr) ? rotate_op->value    : Xform_op_value{};
    const Xform_op_value old_scale_value       = (scale_op     != nullptr) ? scale_op->value     : Xform_op_value{};

    if (translation_changed) {
        translate_op->value = glm::dvec3{translation};
    }
    if (rotation_changed) {
        rotate_op->value = new_rotation_value;
    }
    if (scale_changed) {
        scale_op->value = glm::dvec3{scale};
    }

    // The ops that were not written still contribute: a pivot pair or an
    // inverted op moves the result away from the requested transform. The
    // stack carries the edit only when it composes to exactly that.
    if (!is_near(stack.compose(), trs_matrix(translation, rotation, scale))) {
        if (translation_changed) {
            translate_op->value = old_translation_value;
        }
        if (rotation_changed) {
            rotate_op->value = old_rotation_value;
        }
        if (scale_changed) {
            scale_op->value = old_scale_value;
        }
        return Xform_op_write_back_result::not_representable;
    }
    return Xform_op_write_back_result::written;
}

} // namespace erhe::scene
