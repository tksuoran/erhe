#include "erhe_physics/box3d_six_dof_classifier.hpp"
#include "erhe_physics/joint_limits.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace erhe::physics {

auto c_str(const Axis_state axis_state) -> const char*
{
    switch (axis_state) {
        case Axis_state::fixed:   return "fixed";
        case Axis_state::limited: return "limited";
        case Axis_state::free:    return "free";
        default:                  return "?";
    }
}

auto c_str(const Six_dof_joint_kind kind) -> const char*
{
    switch (kind) {
        case Six_dof_joint_kind::weld:      return "weld";
        case Six_dof_joint_kind::revolute:  return "revolute";
        case Six_dof_joint_kind::prismatic: return "prismatic";
        case Six_dof_joint_kind::distance:  return "distance";
        case Six_dof_joint_kind::spherical: return "spherical";
        case Six_dof_joint_kind::filter:    return "filter";
        default:                            return "?";
    }
}

auto classify_axis(const Constraint_axis_limit& limit) -> Axis_state
{
    if (!limit.limited) {
        return Axis_state::free;
    }
    // An inverted range is sanitized to a fixed axis, matching the Jolt backend.
    if (limit.min >= limit.max) {
        return Axis_state::fixed;
    }
    return Axis_state::limited;
}

namespace {

class Group_summary
{
public:
    int fixed_count       {0};
    int non_fixed_count   {0};
    int first_non_fixed   {-1};
    bool all_non_fixed_free{true};
};

[[nodiscard]] auto summarize(const std::array<Axis_state, 6>& axis_states, const int base) -> Group_summary
{
    Group_summary summary{};
    for (int i = 0; i < 3; ++i) {
        const Axis_state state = axis_states[static_cast<std::size_t>(base + i)];
        if (state == Axis_state::fixed) {
            ++summary.fixed_count;
            continue;
        }
        if (summary.first_non_fixed < 0) {
            summary.first_non_fixed = i;
        }
        ++summary.non_fixed_count;
        if (state != Axis_state::free) {
            summary.all_non_fixed_free = false;
        }
    }
    return summary;
}

[[nodiscard]] auto same_range(const Constraint_axis_limit& lhs, const Constraint_axis_limit& rhs) -> bool
{
    return (lhs.min == rhs.min) && (lhs.max == rhs.max);
}

// The rotation axis a spherical joint twists about; see the header.
[[nodiscard]] auto choose_twist_axis(const std::array<Axis_state, 6>& axis_states, const std::array<Constraint_axis_limit, 6>& limits) -> int
{
    int fixed_axis   = -1;
    int limited_axis = -1;
    int free_axis    = -1;
    int fixed_count   = 0;
    int limited_count = 0;
    int free_count    = 0;
    for (int axis = 0; axis < 3; ++axis) {
        switch (axis_states[static_cast<std::size_t>(3 + axis)]) {
            case Axis_state::fixed:   ++fixed_count;   fixed_axis   = axis; break;
            case Axis_state::limited: ++limited_count; limited_axis = axis; break;
            default:                  ++free_count;    free_axis    = axis; break;
        }
    }
    if (fixed_count == 1) {
        return fixed_axis;
    }
    if ((limited_count == 1) && (free_count == 2)) {
        return limited_axis;
    }
    if ((limited_count == 2) && (free_count == 1)) {
        return free_axis;
    }
    if (limited_count == 3) {
        // Two equal ranges are a cone; the odd one out is the twist.
        const Constraint_axis_limit& x = limits[3];
        const Constraint_axis_limit& y = limits[4];
        const Constraint_axis_limit& z = limits[5];
        if (same_range(y, z) && !same_range(x, y)) { return 0; }
        if (same_range(x, z) && !same_range(x, y)) { return 1; }
    }
    return 2;
}

// The three translation ranges read as one radial range (the inscribed
// sphere). Free axes do not narrow it.
void fill_distance_range(const std::array<Axis_state, 6>& axis_states, const std::array<Constraint_axis_limit, 6>& limits, Six_dof_classification& classification)
{
    float min_distance = 0.0f;
    float max_distance = std::numeric_limits<float>::infinity();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (axis_states[axis] != Axis_state::limited) {
            continue;
        }
        const Constraint_axis_limit& limit = limits[axis];
        const float reach = std::max(std::abs(limit.min), std::abs(limit.max));
        max_distance = std::min(max_distance, reach);
        min_distance = std::max(min_distance, limit.min);
    }
    classification.min_distance = std::isfinite(max_distance) ? std::min(min_distance, max_distance) : min_distance;
    classification.max_distance = max_distance;
}

} // anonymous namespace

auto classify_six_dof(const std::array<Constraint_axis_limit, 6>& limits) -> Six_dof_classification
{
    Six_dof_classification classification{};
    for (std::size_t i = 0; i < 6; ++i) {
        classification.axis_states[i] = classify_axis(limits[i]);
    }

    const Group_summary translation = summarize(classification.axis_states, 0);
    const Group_summary rotation    = summarize(classification.axis_states, 3);

    // Fully locked.
    if ((translation.non_fixed_count == 0) && (rotation.non_fixed_count == 0)) {
        classification.kind     = Six_dof_joint_kind::weld;
        classification.is_exact = true;
        return classification;
    }

    // Fully free: nothing to constrain. The joint still exists so that the
    // caller can apply its collision-exclusion behavior.
    if ((translation.non_fixed_count == 3) && (rotation.non_fixed_count == 3) &&
        translation.all_non_fixed_free && rotation.all_non_fixed_free) {
        classification.kind     = Six_dof_joint_kind::filter;
        classification.is_exact = true;
        return classification;
    }

    if (translation.non_fixed_count == 0) {
        // Pure rotational joint.
        if (rotation.non_fixed_count == 1) {
            classification.kind     = Six_dof_joint_kind::revolute;
            classification.axis     = rotation.first_non_fixed;
            classification.is_exact = true;
            return classification;
        }
        classification.kind = Six_dof_joint_kind::spherical;
        classification.axis = choose_twist_axis(classification.axis_states, limits);
        // A cone limit couples two swing axes rather than limiting each on its
        // own, a universal joint's locked twist admits more than a Hooke joint,
        // and a twist limit about Y or Z is the swing-twist twist, not the
        // contract's pyramid swing angle. Exact: the free ball joint, and a
        // twist-only limit about X.
        const bool twist_only_about_x =
            (classification.axis == 0) &&
            (classification.axis_states[3] == Axis_state::limited) &&
            (classification.axis_states[4] == Axis_state::free) &&
            (classification.axis_states[5] == Axis_state::free);
        classification.is_exact = ((rotation.non_fixed_count == 3) && rotation.all_non_fixed_free) || twist_only_about_x;
        return classification;
    }

    if ((rotation.non_fixed_count == 0) && (translation.non_fixed_count == 1)) {
        classification.kind     = Six_dof_joint_kind::prismatic;
        classification.axis     = translation.first_non_fixed;
        classification.is_exact = true;
        return classification;
    }

    // Rotation free, translation confined: a distance joint keeps the frame
    // origins within a length range. Exact when the three ranges are one
    // radial range (a 3D linear limit); a box of independent ranges gets its
    // inscribed sphere.
    if ((rotation.non_fixed_count == 3) && rotation.all_non_fixed_free && (translation.non_fixed_count == 3)) {
        classification.kind = Six_dof_joint_kind::distance;
        fill_distance_range(classification.axis_states, limits, classification);
        const bool all_limited = !translation.all_non_fixed_free &&
            (classification.axis_states[0] == Axis_state::limited) &&
            (classification.axis_states[1] == Axis_state::limited) &&
            (classification.axis_states[2] == Axis_state::limited);
        classification.is_exact = all_limited &&
            same_range(limits[0], limits[1]) && same_range(limits[0], limits[2]) && (limits[0].min >= 0.0f);
        return classification;
    }

    // Not representable by any single Box3D joint: mixed translational and
    // rotational freedom, or more than one free translation axis. Fall back to
    // whichever joint preserves the most of the intended constraint.
    classification.is_exact = false;
    if (translation.non_fixed_count <= 1) {
        classification.kind = Six_dof_joint_kind::prismatic;
        classification.axis = translation.first_non_fixed;
    } else if (rotation.non_fixed_count == 0) {
        // Planar: a prismatic joint along the wider of the translation axes
        // keeps one of the two degrees of freedom.
        classification.kind = Six_dof_joint_kind::prismatic;
        classification.axis = translation.first_non_fixed;
        float widest = -1.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const Axis_state state = classification.axis_states[static_cast<std::size_t>(axis)];
            if (state == Axis_state::fixed) {
                continue;
            }
            const float width = (state == Axis_state::free)
                ? std::numeric_limits<float>::infinity()
                : (limits[static_cast<std::size_t>(axis)].max - limits[static_cast<std::size_t>(axis)].min);
            if (width > widest) {
                widest = width;
                classification.axis = axis;
            }
        }
    } else if ((translation.fixed_count + rotation.fixed_count) >= 4) {
        classification.kind = Six_dof_joint_kind::weld;
    } else {
        classification.kind = Six_dof_joint_kind::spherical;
        classification.axis = choose_twist_axis(classification.axis_states, limits);
    }
    return classification;
}

auto describe_axis_states(const std::array<Axis_state, 6>& axis_states) -> std::string
{
    std::string result;
    result.reserve(7);
    for (std::size_t i = 0; i < 6; ++i) {
        if (i == 3) {
            result.push_back(' ');
        }
        switch (axis_states[i]) {
            case Axis_state::fixed:   result.push_back('F'); break;
            case Axis_state::limited: result.push_back('L'); break;
            case Axis_state::free:    result.push_back('-'); break;
            default:                  result.push_back('?'); break;
        }
    }
    return result;
}

auto revolute_frame_rotation(const int axis) -> glm::quat
{
    // Carries the selected axis onto +Z.
    switch (axis) {
        case 0: return glm::angleAxis( glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
        case 1: return glm::angleAxis(-glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        default: return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
}

auto prismatic_frame_rotation(const int axis) -> glm::quat
{
    // Carries the selected axis onto +X.
    switch (axis) {
        case 1: return glm::angleAxis( glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
        case 2: return glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
        default: return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
}

auto reduced_mass(const float mass_a, const float mass_b) -> float
{
    const bool a_is_finite = (mass_a > 0.0f) && std::isfinite(mass_a);
    const bool b_is_finite = (mass_b > 0.0f) && std::isfinite(mass_b);
    if (a_is_finite && b_is_finite) {
        return (mass_a * mass_b) / (mass_a + mass_b);
    }
    if (a_is_finite) {
        return mass_a;
    }
    if (b_is_finite) {
        return mass_b;
    }
    return 0.0f;
}

auto stiffness_to_hertz(const float stiffness, const float mass) -> float
{
    if ((stiffness <= 0.0f) || (mass <= 0.0f) || !std::isfinite(stiffness) || !std::isfinite(mass)) {
        return 0.0f;
    }
    return std::sqrt(stiffness / mass) / glm::two_pi<float>();
}

auto damping_to_ratio(const float damping, const float stiffness, const float effective_mass) -> float
{
    if ((damping <= 0.0f) || (stiffness <= 0.0f) || (effective_mass <= 0.0f) ||
        !std::isfinite(damping) || !std::isfinite(stiffness) || !std::isfinite(effective_mass)) {
        return 0.0f;
    }
    return damping / (2.0f * std::sqrt(stiffness * effective_mass));
}

auto drive_to_box3d_spring(const Constraint_axis_drive& drive, const float effective_mass) -> Box3d_spring
{
    if (drive.mode == Drive_force_mode::acceleration) {
        // Mass normalized already: k / m = omega^2, c / m = 2 zeta omega.
        return Box3d_spring{
            .hertz         = stiffness_to_hertz(drive.stiffness, 1.0f),
            .damping_ratio = damping_to_ratio(drive.damping, drive.stiffness, 1.0f)
        };
    }
    return Box3d_spring{
        .hertz         = stiffness_to_hertz(drive.stiffness, effective_mass),
        .damping_ratio = damping_to_ratio(drive.damping, drive.stiffness, effective_mass)
    };
}

auto describe_box3d_incompatibility(const std::array<Constraint_axis_limit, 6>& limits) -> std::optional<std::string>
{
    static constexpr const char* c_axis_names[3] = {"X", "Y", "Z"};

    // What the backend does: fold the fixed values into frame A, then classify.
    Transform                            folded_frame{};
    std::array<Constraint_axis_limit, 6> folded_limits = limits;
    const Fixed_axis_fold                fold          = fold_fixed_axis_values(folded_frame, folded_limits);
    const Six_dof_classification         classification = classify_six_dof(folded_limits);

    std::string message;
    if (!classification.is_exact) {
        const std::string pattern = describe_axis_states(classification.axis_states);
        switch (classification.kind) {
            case Six_dof_joint_kind::spherical: {
                const std::array<int, 2> swing_axes = get_swing_axes(classification.axis);
                message = "Box3D has no joint for the axis pattern " + pattern +
                    " (translation fixed / limited / free, then rotation): it simulates a ball joint twisting about rotation " +
                    c_axis_names[classification.axis] + " with one cone of the widest limited swing range (rotation " +
                    c_axis_names[swing_axes[0]] + " or " + c_axis_names[swing_axes[1]] + ") about that axis.";
                break;
            }
            case Six_dof_joint_kind::distance: {
                message = "Box3D has no joint for the axis pattern " + pattern +
                    ": it simulates a distance joint keeping the frame origins within the sphere inscribed in the translation ranges.";
                break;
            }
            default: {
                message = "Box3D has no joint for the axis pattern " + pattern + ": it simulates the closest " +
                    std::string{c_str(classification.kind)} + " joint" +
                    ((classification.kind == Six_dof_joint_kind::prismatic) ? std::string{" along translation "} + c_axis_names[classification.axis] : std::string{}) + ".";
                break;
            }
        }
    }
    if (!fold.exact) {
        if (!message.empty()) {
            message += " ";
        }
        message += "A fixed rotation at a non-zero angle is fixed at 0 while a translation axis is not fixed.";
    }
    if (message.empty()) {
        return std::nullopt;
    }
    return message;
}

} // namespace erhe::physics
