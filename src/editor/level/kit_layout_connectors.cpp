#include "level/kit_layout_connectors.hpp"

#include <cmath>

namespace editor {

auto c_str(const Kit_layout_direction direction) -> const char*
{
    switch (direction) {
        case Kit_layout_direction::plus_tangent:   return "+tangent";
        case Kit_layout_direction::minus_tangent:  return "-tangent";
        case Kit_layout_direction::plus_normal:    return "+normal";
        case Kit_layout_direction::minus_normal:   return "-normal";
        case Kit_layout_direction::plus_binormal:  return "+binormal";
        case Kit_layout_direction::minus_binormal: return "-binormal";
        default:                                   return "?";
    }
}

auto c_str(const Kit_layout_child_role role) -> const char*
{
    switch (role) {
        case Kit_layout_child_role::inner: return "inner";
        case Kit_layout_child_role::head:  return "head";
        case Kit_layout_child_role::tail:  return "tail";
        case Kit_layout_child_role::only:  return "only";
        default:                           return "?";
    }
}

auto c_str(const Kit_layout_hide_reason reason) -> const char*
{
    switch (reason) {
        case Kit_layout_hide_reason::shown:     return "shown";
        case Kit_layout_hide_reason::hide_all:  return "hide_all";
        case Kit_layout_hide_reason::direction: return "direction";
        default:                                return "?";
    }
}

auto Kit_layout_connector_hiding::is_hidden(const Kit_layout_direction direction) const -> bool
{
    return directions[static_cast<std::size_t>(direction)] == Kit_layout_connector_visibility::hidden;
}

auto Kit_layout_connector_hiding::hides_any() const -> bool
{
    if (all == Kit_layout_connector_visibility::hidden) {
        return true;
    }
    for (const Kit_layout_connector_visibility visibility : directions) {
        if (visibility == Kit_layout_connector_visibility::hidden) {
            return true;
        }
    }
    return false;
}

void Kit_layout_connector_hiding::set(const Kit_layout_direction direction, const Kit_layout_connector_visibility visibility)
{
    directions[static_cast<std::size_t>(direction)] = visibility;
}

auto get_layout_child_role(const std::size_t index, const std::size_t count, const erhe::math::Curve_closure closure) -> Kit_layout_child_role
{
    if (count <= 1) {
        return Kit_layout_child_role::only;
    }
    if (closure == erhe::math::Curve_closure::closed) {
        return Kit_layout_child_role::inner;
    }
    if (index == 0) {
        return Kit_layout_child_role::head;
    }
    if (index + 1 >= count) {
        return Kit_layout_child_role::tail;
    }
    return Kit_layout_child_role::inner;
}

auto classify_layout_direction(const glm::vec3& outward_in_element) -> Kit_layout_direction
{
    const float tangent  = outward_in_element.x;
    const float binormal = outward_in_element.y;
    const float normal   = outward_in_element.z;
    const float abs_tangent  = std::abs(tangent);
    const float abs_binormal = std::abs(binormal);
    const float abs_normal   = std::abs(normal);
    if ((abs_tangent >= abs_normal) && (abs_tangent >= abs_binormal)) {
        return (tangent >= 0.0f) ? Kit_layout_direction::plus_tangent : Kit_layout_direction::minus_tangent;
    }
    if (abs_normal >= abs_binormal) {
        return (normal >= 0.0f) ? Kit_layout_direction::plus_normal : Kit_layout_direction::minus_normal;
    }
    return (binormal >= 0.0f) ? Kit_layout_direction::plus_binormal : Kit_layout_direction::minus_binormal;
}

auto get_outward_in_element(const glm::vec3& local_outward, const glm::vec3& scale) -> std::optional<glm::vec3>
{
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(scale[axis]) || !(std::abs(scale[axis]) > 1.0e-6f)) {
            return {};
        }
    }
    // conj(R) * (R * S)^-T * n = S^-1 * n: the face normal transform_connector() carries (D1.12), in the element frame.
    const glm::vec3 outward = local_outward / scale;
    const float     length  = glm::length(outward);
    if (!(length > 0.0f) || !std::isfinite(length)) {
        return {};
    }
    return outward / length;
}

auto get_layout_hide_reason(const Kit_layout_connector_hiding& hiding, const Kit_layout_child_role role, const Kit_layout_direction direction) -> Kit_layout_hide_reason
{
    switch (role) {
        case Kit_layout_child_role::only: return Kit_layout_hide_reason::shown;
        case Kit_layout_child_role::head: if (direction != Kit_layout_direction::plus_tangent)  { return Kit_layout_hide_reason::shown; } break;
        case Kit_layout_child_role::tail: if (direction != Kit_layout_direction::minus_tangent) { return Kit_layout_hide_reason::shown; } break;
        case Kit_layout_child_role::inner:
        default: break;
    }
    if (hiding.all == Kit_layout_connector_visibility::hidden) {
        return Kit_layout_hide_reason::hide_all;
    }
    return hiding.is_hidden(direction) ? Kit_layout_hide_reason::direction : Kit_layout_hide_reason::shown;
}

auto get_layout_connector_verdict(const Kit_layout_child& child, const glm::vec3& local_outward) -> Kit_layout_connector_verdict
{
    Kit_layout_connector_verdict verdict;
    const std::optional<glm::vec3> outward = get_outward_in_element(local_outward, child.scale);
    if (!outward.has_value()) {
        return verdict;
    }
    verdict.direction = classify_layout_direction(outward.value());
    verdict.reason    = get_layout_hide_reason(child.hiding, child.role, verdict.direction.value());
    return verdict;
}

} // namespace editor
