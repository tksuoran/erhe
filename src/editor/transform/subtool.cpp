#include "transform/subtool.hpp"

#include "scene/scene_view.hpp"
#include "transform/handle_enums.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using namespace glm;

Subtool::Subtool(App_context& app_context)
    : Tool{app_context}
{
}

Subtool::Subtool(App_context& app_context, Tools& tools, const uint64_t flags)
    : Tool{app_context, tools, flags}
{
}

Subtool::~Subtool() noexcept = default;

void Subtool::imgui(Property_editor&)
{
}

auto Subtool::begin_drag(const unsigned int axis_mask, Scene_view* scene_view) -> bool
{
    m_active = begin(axis_mask, scene_view);
    return m_active;
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
    if (m_record_operation) {
        m_record_operation();
    }
    m_active = false;
}

void Subtool::set_transform_shared(Transform_tool_shared& shared, std::function<void()> record_operation)
{
    m_shared           = &shared;
    m_record_operation = std::move(record_operation);
}

auto Subtool::get_shared() const -> Transform_tool_shared&
{
    ERHE_VERIFY(m_shared != nullptr);
    return *m_shared;
}

auto Subtool::get_basis() const -> glm::mat3
{
    const auto& shared = get_shared();
    return get_basis(!shared.settings.use_anchor_orientation());
}

// The drag basis carries only the anchor's orientation, never its scale or
// skew - the same rule Handle_visualizations::get_basis() uses to place the
// handles, so the axes dragged are the axes drawn. Taking the columns of the
// anchor's world matrix instead would hand out non-unit (and, under a
// non-uniform parent scale, non-orthogonal) axes, and a rotation built about
// a non-unit axis is not a rotation at all - it injects scale and shear.
auto Subtool::get_basis(const bool world) const -> glm::mat3
{
    const auto& shared = get_shared();

    return world
        ? glm::mat3{1.0f}
        : glm::mat3_cast(shared.world_from_anchor_initial_state.get_rotation());
}

auto Subtool::get_axis_direction() const -> vec3
{
    const glm::mat3 basis = get_basis();
    switch (m_axis_mask) {
        case Axis_mask::x:  return vec3{basis[0]};
        case Axis_mask::yz: return vec3{basis[0]};
        case Axis_mask::y:  return vec3{basis[1]};
        case Axis_mask::xz: return vec3{basis[1]};
        case Axis_mask::z:  return vec3{basis[2]};
        case Axis_mask::xy: return vec3{basis[2]};
        default: {
            ERHE_FATAL("get_axis_direction() failed for axis mask %02x", m_axis_mask);
            break;
        }
    }
}

auto Subtool::get_plane_normal(const bool world) const -> vec3
{
    const glm::mat3 basis = get_basis(world);
    switch (m_axis_mask) {
        case Axis_mask::x:  return vec3{basis[0]};
        case Axis_mask::yz: return vec3{basis[0]};
        case Axis_mask::y:  return vec3{basis[1]};
        case Axis_mask::xz: return vec3{basis[1]};
        case Axis_mask::z:  return vec3{basis[2]};
        case Axis_mask::xy: return vec3{basis[2]};
        default: {
            ERHE_FATAL("get_plane_normal(): bad axis mask = %02x", m_axis_mask);
            break;
        }
    }
}

auto Subtool::get_plane_side(const bool world) const -> vec3
{
    const glm::mat3 basis = get_basis(world);
    switch (m_axis_mask) {
        case Axis_mask::x:  return vec3{basis[1]};
        case Axis_mask::yz: return vec3{basis[1]};
        case Axis_mask::y:  return vec3{basis[2]};
        case Axis_mask::xz: return vec3{basis[2]};
        case Axis_mask::z:  return vec3{basis[0]};
        case Axis_mask::xy: return vec3{basis[0]};
        default: {
            ERHE_FATAL("get_plane_side(): bad axis mask = %02x", m_axis_mask);
            break;
        }
    }
}

#pragma region Helpers

auto get_label_color(
    const std::size_t i,
    const bool        text,
    const bool        matches_gizmo
) -> uint32_t
{
    if (!matches_gizmo) {
        return text ? 0xffccccccu : 0xff222222u;
    }
    switch (i) {
        case 0:  return text ? 0xff8888ffu : 0xff222266u; // X
        case 1:  return text ? 0xff88ff88u : 0xff226622u; // Y
        case 2:  return text ? 0xffff8888u : 0xff662222u; // Z
        case 3:  return text ? 0xffff88ffu : 0xff662266u; // W
        default: return text ? 0xffccccccu : 0xff222222u;
    }
}

auto get_drag_color(const std::size_t i, bool locked) -> ImVec4
{
    if (locked) {
        return ImVec4(0.33f, 0.33f, 0.33f, 1.0f);
    }
    switch (i) {
        case 0:  return ImVec4(1.00f, 0.08f, 0.08f, 1.0f);
        case 1:  return ImVec4(0.08f, 1.00f, 0.08f, 1.0f);
        case 2:  return ImVec4(0.08f, 0.08f, 1.00f, 1.0f);
        case 3:  return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        default: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    }
}

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

}
