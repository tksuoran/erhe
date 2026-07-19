#include "erhe_physics/box3d/box3d_six_dof_classifier.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>

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
        // A spherical joint always exposes all three rotational degrees of
        // freedom, so a two-axis (universal) joint is over-permissive, and its
        // cone limit couples two axes rather than limiting each independently.
        classification.is_exact = (rotation.non_fixed_count == 3) && rotation.all_non_fixed_free;
        return classification;
    }

    if ((rotation.non_fixed_count == 0) && (translation.non_fixed_count == 1)) {
        classification.kind     = Six_dof_joint_kind::prismatic;
        classification.axis     = translation.first_non_fixed;
        classification.is_exact = true;
        return classification;
    }

    // Not representable by any single Box3D joint: mixed translational and
    // rotational freedom, or more than one free translation axis. Fall back to
    // whichever joint preserves the most of the intended constraint.
    classification.is_exact = false;
    if (translation.non_fixed_count <= 1) {
        classification.kind = Six_dof_joint_kind::prismatic;
        classification.axis = translation.first_non_fixed;
    } else if ((translation.fixed_count + rotation.fixed_count) >= 4) {
        classification.kind = Six_dof_joint_kind::weld;
    } else {
        classification.kind = Six_dof_joint_kind::spherical;
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

} // namespace erhe::physics
