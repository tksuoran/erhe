#include "tools/transform/subtool.hpp"

#include "editor_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/transform_tool.hpp"

#include "erhe_verify/verify.hpp"

namespace editor {

using namespace glm;

Subtool::Subtool(Editor_context& editor_context)
    : Tool{editor_context}
{
}

Subtool::~Subtool() noexcept = default;

void Subtool::imgui(Property_editor&)
{
}

auto Subtool::is_active() const -> bool
{
    return m_active;
}

auto Subtool::get_axis_mask() const -> unsigned int
{
    return m_axis_mask;
}

void Subtool::end()
{
    m_context.transform_tool->record_transform_operation();
    m_active = false;
}

namespace {

constexpr glm::mat4 mat4_identity{1.0f};

};

auto Subtool::get_shared() const -> Transform_tool_shared&
{
    return m_context.transform_tool->shared;
}

auto Subtool::get_basis() const -> const glm::mat4&
{
    const auto& shared = get_shared();
    return shared.settings.local 
        ? shared.world_from_anchor_initial_state.get_matrix()
        : mat4_identity;
}

auto Subtool::get_basis(const bool world) const -> const glm::mat4&
{
    const auto& shared = get_shared();

    return world
        ? mat4_identity
        : shared.world_from_anchor_initial_state.get_matrix();
}

auto Subtool::get_axis_direction() const -> vec3
{
    const glm::mat4& basis = get_basis();
    switch (m_axis_mask) {
        case Axis_mask::x:  return basis[0];
        case Axis_mask::yz: return basis[0];
        case Axis_mask::y:  return basis[1];
        case Axis_mask::xz: return basis[1];
        case Axis_mask::z:  return basis[2];
        case Axis_mask::xy: return basis[2];
        default: {
            ERHE_FATAL("get_axis_direction() failed for axis mask %02x", m_axis_mask);
            break;
        }
    }
}

auto Subtool::get_plane_normal(const bool world) const -> vec3
{
    const glm::mat4& basis = get_basis(world);
    switch (m_axis_mask) {
        case Axis_mask::x:  return basis[0];
        case Axis_mask::yz: return basis[0];
        case Axis_mask::y:  return basis[1];
        case Axis_mask::xz: return basis[1];
        case Axis_mask::z:  return basis[2];
        case Axis_mask::xy: return basis[2];
        default: {
            ERHE_FATAL("get_plane_normal(): bad axis mask = %02x", m_axis_mask);
            break;
        }
    }
}

auto Subtool::get_plane_side(const bool world) const -> vec3
{
    const glm::mat4& basis = get_basis(world);
    switch (m_axis_mask) {
        case Axis_mask::x:  return basis[1];
        case Axis_mask::yz: return basis[1];
        case Axis_mask::y:  return basis[2];
        case Axis_mask::xz: return basis[2];
        case Axis_mask::z:  return basis[0];
        case Axis_mask::xy: return basis[0];
        default: {
            ERHE_FATAL("get_plane_side(): bad axis mask = %02x", m_axis_mask);
            break;
        }
    }
}

#pragma region Helpers

auto Subtool::offset_plane_origo(const vec3 p) const -> vec3
{
    switch (m_axis_mask) {
        case Axis_mask::x: return vec3{ p.x, 0.0f, 0.0f};
        case Axis_mask::y: return vec3{0.0f,  p.y, 0.0f};
        case Axis_mask::z: return vec3{0.0f, 0.0f,  p.z};
        default:
            ERHE_FATAL("offset_plane_origo(): bad axis mask = %02x", m_axis_mask);
            break;
    }
}

auto Subtool::project_to_offset_plane(const vec3 P, const vec3 Q) const -> vec3
{
    switch (m_axis_mask) {
        case Axis_mask::x: return vec3{P.x, Q.y, Q.z};
        case Axis_mask::y: return vec3{Q.x, P.y, Q.z};
        case Axis_mask::z: return vec3{Q.x, Q.y, P.z};
        default:
            ERHE_FATAL("project_to_offset_plane(): bad axis mask = %02x", m_axis_mask);
            break;
    }
}

auto Subtool::project_pointer_to_plane(Scene_view* scene_view, const vec3 n, const vec3 p) -> std::optional<vec3>
{
    if (scene_view == nullptr) {
        return {};
    }

    const auto origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto direction_opt = scene_view->get_control_ray_direction_in_world();
    if (
        !origin_opt.has_value() ||
        !direction_opt.has_value()
    ) {
        return {};
    }

    const vec3 q0           = origin_opt.value();
    const vec3 v            = direction_opt.value();
    const auto intersection = erhe::math::intersect_plane<float>(n, p, q0, v);
    if (intersection.has_value()) {
        return q0 + intersection.value() * v;
    }
    return {};
}

#pragma endregion Helpers

} // namespace editor
