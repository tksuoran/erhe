#include "transform/rotation_inspector.hpp"
#include "transform/subtool.hpp"
#include "windows/property_editor.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_math/euler_angles.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

namespace editor {

using glm::normalize;
using glm::cross;
using glm::dot;
using glm::distance;
using glm::mat3_cast;
using glm::mat4_cast;
using glm::quat_cast;
using mat3 = glm::mat3;
using mat4 = glm::mat4;
using quat = glm::quat;
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

using Trs_transform = erhe::scene::Trs_transform;

Rotation_inspector::Rotation_inspector()
{
    m_euler_angles[0] = 0.0f;
    m_euler_angles[1] = 0.0f;
    m_euler_angles[2] = 0.0f;
}

void Rotation_inspector::set_representation(const Representation representation)
{
    m_representation = representation;
}

void Rotation_inspector::set_euler_order(const Euler_angle_order euler_angle_order)
{
    m_euler_angle_order = euler_angle_order;
}

void Rotation_inspector::set_matrix(const mat3& m)
{
    m_matrix     = mat4{m};
    m_quaternion = quat_cast(m);
    update_euler_angles_from_quaternion();
    update_axis_angle_from_quaternion();
}

void Rotation_inspector::set_quaternion(const quat& q)
{
    m_quaternion = q;
    m_matrix     = mat4{mat3_cast(q)};
    update_euler_angles_from_quaternion();
    update_axis_angle_from_quaternion();
}

void Rotation_inspector::set_axis_angle(const glm::vec3 axis, const float angle)
{
    m_axis       = axis;
    m_angle      = angle;
    m_quaternion = glm::angleAxis(angle, axis);
    m_matrix     = mat4{mat3_cast(m_quaternion)};
    update_euler_angles_from_quaternion();
}

void Rotation_inspector::set_active(const bool active)
{
    m_active = active;
}

auto Rotation_inspector::is_proper(const Euler_angle_order euler_angle_order) -> bool
{
    switch (euler_angle_order) {
        case Euler_angle_order::e_xyx: return true;
        case Euler_angle_order::e_xyz: return false;
        case Euler_angle_order::e_xzx: return true;
        case Euler_angle_order::e_xzy: return false;
        case Euler_angle_order::e_yxy: return true;
        case Euler_angle_order::e_yxz: return false;
        case Euler_angle_order::e_yzx: return false;
        case Euler_angle_order::e_yzy: return true;
        case Euler_angle_order::e_zxy: return false;
        case Euler_angle_order::e_zxz: return true;
        case Euler_angle_order::e_zyx: return false;
        case Euler_angle_order::e_zyz: return true;
        default: return false;
    }
}

auto Rotation_inspector::is_tait_bryan(const Euler_angle_order euler_angle_order) -> bool
{
    switch (euler_angle_order) {
        case Euler_angle_order::e_xyx: return !true;
        case Euler_angle_order::e_xyz: return !false;
        case Euler_angle_order::e_xzx: return !true;
        case Euler_angle_order::e_xzy: return !false;
        case Euler_angle_order::e_yxy: return !true;
        case Euler_angle_order::e_yxz: return !false;
        case Euler_angle_order::e_yzx: return !false;
        case Euler_angle_order::e_yzy: return !true;
        case Euler_angle_order::e_zxy: return !false;
        case Euler_angle_order::e_zxz: return !true;
        case Euler_angle_order::e_zyx: return !false;
        case Euler_angle_order::e_zyz: return !true;
        default: return false;
    }
}

auto Rotation_inspector::get_euler_axis2(const Euler_angle_order euler_angle_order, const int i) -> int
{
    ERHE_VERIFY(i >= 0 && i < 3);
    switch (euler_angle_order) {
        case Euler_angle_order::e_xyx: return std::array<int, 3>{axis_x, axis_y, axis_x2}[i];
        case Euler_angle_order::e_xyz: return std::array<int, 3>{axis_x, axis_y, axis_z }[i];
        case Euler_angle_order::e_xzx: return std::array<int, 3>{axis_x, axis_z, axis_x2}[i];
        case Euler_angle_order::e_xzy: return std::array<int, 3>{axis_x, axis_z, axis_y }[i];
        case Euler_angle_order::e_yxy: return std::array<int, 3>{axis_y, axis_x, axis_y2}[i];
        case Euler_angle_order::e_yxz: return std::array<int, 3>{axis_y, axis_x, axis_z }[i];
        case Euler_angle_order::e_yzx: return std::array<int, 3>{axis_y, axis_z, axis_x }[i];
        case Euler_angle_order::e_yzy: return std::array<int, 3>{axis_y, axis_z, axis_y2}[i];
        case Euler_angle_order::e_zxy: return std::array<int, 3>{axis_z, axis_x, axis_y }[i];
        case Euler_angle_order::e_zxz: return std::array<int, 3>{axis_z, axis_x, axis_z2}[i];
        case Euler_angle_order::e_zyx: return std::array<int, 3>{axis_z, axis_y, axis_x }[i];
        case Euler_angle_order::e_zyz: return std::array<int, 3>{axis_z, axis_y, axis_z2}[i];
        default: return 0;
    }
}

auto Rotation_inspector::get_euler_axis(const Euler_angle_order euler_angle_order, const int i) -> int
{
    return get_euler_axis2(euler_angle_order,i) & axis_xyzw_mask;
}

auto Rotation_inspector::gimbal_lock_warning() const -> float
{
    return gimbal_lock_warning(m_euler_angle_order, m_euler_angles[1]);
}

auto Rotation_inspector::gimbal_lock_warning(const Euler_angle_order euler_angle_order, const float middle_angle) -> float
{
    using namespace std;
    if (is_proper(euler_angle_order)) {
        const float modulo   = fmodf(middle_angle, glm::pi<float>());
        const float distance = std::min(
            std::abs(modulo),
            std::abs(modulo - glm::pi<float>())
        );
        return (distance < 0.15) ? erhe::math::remap(distance, 0.15f, 0.0f, 0.0f, 1.0f) : 0.0f;
    } else {
        const float modulo   = fmodf(middle_angle + glm::half_pi<float>(), glm::pi<float>());
        const float distance = std::min(
            std::abs(modulo),
            std::abs(modulo - glm::pi<float>())
        );
        return (distance < 0.15) ? erhe::math::remap(distance, 0.15f, 0.0f, 0.0f, 1.0f) : 0.0f;
    }
}

void Rotation_inspector::update_axis_angle_from_quaternion()
{
    m_axis  = glm::axis (m_quaternion);
    m_angle = glm::angle(m_quaternion);
}

void Rotation_inspector::update_euler_angles_from_quaternion()
{
    // From the quaternion, not the matrix: the matrix cannot tell q from -q,
    // the quaternion extraction gives q and -q different angles, so editing
    // the angles and reading them back keeps the quaternion's sign.
    //
    // Many angle triples give the same quaternion (full turns on two angles,
    // the second Euler branch, any split at gimbal lock). While the angles
    // shown still give (nearly) this quaternion - the rotation read back after
    // an edit here ended, or moved a small step by the gizmo - the triple
    // nearest to them is shown, so angles dragged or typed past +-180 deg stay
    // as edited and gizmo rotation moves them continuously. A rotation that
    // jumped (another node selected, undo, a value set from elsewhere) owes
    // nothing to the angles shown before and gets the canonical triple.
    const int       axis_1 = get_euler_axis(m_euler_angle_order, 0);
    const int       axis_2 = get_euler_axis(m_euler_angle_order, 1);
    const int       axis_3 = get_euler_axis(m_euler_angle_order, 2);
    const glm::quat q      = normalize(m_quaternion);
    const glm::quat shown  = erhe::math::euler_angles_to_quaternion(
        axis_1, axis_2, axis_3, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]
    );
    // Sign-aware: -q (a full turn away) counts as a jump.
    constexpr float c_follow_max_rotation = glm::radians(30.0f);
    const bool follows_shown_angles = glm::dot(shown, q) >= std::cos(0.5f * c_follow_max_rotation);
    if (follows_shown_angles) {
        const float reference[3] = {m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]};
        erhe::math::quaternion_to_euler_angles_near(
            q, axis_1, axis_2, axis_3,
            reference[0], reference[1], reference[2],
            m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]
        );
    } else {
        erhe::math::quaternion_to_euler_angles(
            q, axis_1, axis_2, axis_3,
            m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]
        );
    }
}

void Rotation_inspector::update_matrix_and_quaternion_from_euler_angles()
{
    m_quaternion = erhe::math::euler_angles_to_quaternion(
        get_euler_axis(m_euler_angle_order, 0),
        get_euler_axis(m_euler_angle_order, 1),
        get_euler_axis(m_euler_angle_order, 2),
        m_euler_angles[0],
        m_euler_angles[1],
        m_euler_angles[2]
    );
    m_matrix = mat4{mat3_cast(m_quaternion)};
}

void Rotation_inspector::update_from_axis_angle()
{
    set_axis_angle(m_axis, m_angle);
}

void Rotation_inspector::update_from_quaternion()
{
    m_matrix = mat4{mat3_cast(normalize(m_quaternion))};
    update_euler_angles_from_quaternion();
}

void Rotation_inspector::imgui(
    erhe::imgui::Value_edit_state& quaternion_state,
    erhe::imgui::Value_edit_state& euler_state,
    erhe::imgui::Value_edit_state& axis_angle_state,
    const glm::quat                rotation,
    const bool                     matches_gizmo,
    Property_editor&               property_editor
)
{
    Property_editor& p = property_editor;

    if (!m_active) {
        set_quaternion(rotation);
    }
    
    p.add_entry("Mode", [this]() {
        erhe::imgui::make_combo(
            "##",
            m_representation,
            Rotation_inspector::c_representation_strings,
            IM_ARRAYSIZE(Rotation_inspector::c_representation_strings)
        );
    });

    if (m_representation == Representation::e_euler_angles) {
        p.add_entry("Order", [this]() {
            erhe::imgui::make_combo(
                "##",
                m_euler_angle_order,
                Rotation_inspector::c_euler_strings,
                IM_ARRAYSIZE(Rotation_inspector::c_euler_strings)
            );
        });
    }

    switch (m_representation) {
        case Representation::e_matrix: {
            p.add_entry("Matrix", [this]() {
                const glm::vec3 col0 = glm::vec3{m_matrix[0]};
                const glm::vec3 col1 = glm::vec3{m_matrix[1]};
                const glm::vec3 col2 = glm::vec3{m_matrix[2]};
                ImGui::BeginTable("Matrix", 3, ImGuiTableFlags_None, ImVec2{130.0f, 0.0});
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col0[0]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col1[0]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col2[0]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col0[1]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col1[1]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col2[1]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col0[2]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col1[2]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", col2[2]);
                ImGui::EndTable();
            });
            break;
        }

        case Representation::e_quaternion: {
            p.add_entry(
                "Quaternion",
                [this, &quaternion_state, matches_gizmo]() {
                    glm::vec4 q = glm::vec4{m_quaternion.w, m_quaternion.x, m_quaternion.y, m_quaternion.z};
                    ImGuiSliderFlags flags = ImGuiSliderFlags_NoRoundToFormat;
                    erhe::imgui::Value_edit_state q_edit_state = erhe::imgui::make_drag_vec4(q, {}, {}, 0.02f, flags, "##Quaternion", "%.4f");
                    if (q_edit_state.value_changed) {
                        m_quaternion = glm::quat(q.w, q.x, q.y, q.z);
                    }
                    quaternion_state.combine(q_edit_state);
                }
            );

            //p.add_entry("W", get_label_color(3, true, matches_gizmo), get_label_color(3, false, matches_gizmo), [this, &quaternion_state]() {
            //    quaternion_state.combine(
            //        erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qw")
            //    );
            //});
            //p.add_entry("X", get_label_color(0, true, matches_gizmo), get_label_color(0, false, matches_gizmo), [this, &quaternion_state]() {
            //    quaternion_state.combine(
            //        erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qx")
            //    );
            //});
            //p.add_entry("Y", get_label_color(1, true, matches_gizmo), get_label_color(1, false, matches_gizmo), [this, &quaternion_state]() {
            //    quaternion_state.combine(
            //        erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qy")
            //    );
            //});
            //p.add_entry("Z", get_label_color(2, true, matches_gizmo), get_label_color(2, false, matches_gizmo), [this, &quaternion_state]() {
            //    quaternion_state.combine(
            //        erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qz")
            //    );
            //});
            break;
        }

        case Representation::e_euler_angles: {
            p.add_entry(
                c_euler_strings[static_cast<int>(m_euler_angle_order)],
                [this, &euler_state, matches_gizmo]() {
                    const float warn = gimbal_lock_warning();
                    if (warn > 0.0f) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 1.0f - warn, 0.0f, 1.0f});
                    }
                    glm::vec3 e = glm::vec3{
                        glm::degrees<float>(m_euler_angles[0]),
                        glm::degrees<float>(m_euler_angles[1]),
                        glm::degrees<float>(m_euler_angles[2])
                    };
                    ImGui::PushStyleColor(ImGuiCol_ColorMarker0, get_drag_color(get_euler_axis2(m_euler_angle_order, 0), false));
                    ImGui::PushStyleColor(ImGuiCol_ColorMarker1, get_drag_color(get_euler_axis2(m_euler_angle_order, 1), false));
                    ImGui::PushStyleColor(ImGuiCol_ColorMarker2, get_drag_color(get_euler_axis2(m_euler_angle_order, 2), false));
                    glm::vec4 q = glm::vec4{m_quaternion.w, m_quaternion.x, m_quaternion.y, m_quaternion.z};
                    ImGuiSliderFlags flags = ImGuiSliderFlags_NoRoundToFormat | (matches_gizmo ? ImGuiSliderFlags_ColorMarkers : 0);
                    erhe::imgui::Value_edit_state e_edit_state = erhe::imgui::make_drag_vec3(e, {}, {}, 1.0f, flags, "##Euler", "%.2f\xc2\xb0");
                    ImGui::PopStyleColor(3);
                    if (e_edit_state.value_changed) {
                        m_euler_angles[0] = glm::radians<float>(e.x);
                        m_euler_angles[1] = glm::radians<float>(e.y);
                        m_euler_angles[2] = glm::radians<float>(e.z);
                    }
                    euler_state.combine(e_edit_state);
                    if (warn > 0.0f) {
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted("Gimbal Lock");
                            ImGui::EndTooltip();
                        }
                        ImGui::PopStyleColor(1);
                    }
                }
            );

            break;
        }

        case Representation::e_axis_angle: {
            p.add_entry("Angle", get_label_color(3, true, false), get_label_color(3, false, false), [this, &axis_angle_state]() {
                axis_angle_state.combine(erhe::imgui::make_angle_button(m_angle, -10.0f * glm::pi<float>(), 10.0f * glm::pi<float>(), "##R.aa"));
            });
            p.add_entry("X", get_label_color(0, true, false), get_label_color(0, false, false), [this, &axis_angle_state]() {
                axis_angle_state.combine(erhe::imgui::make_scalar_button(&m_axis.x, -1.0f, 1.0f, "##R.ax"));
            });
            p.add_entry("Y", get_label_color(1, true, false), get_label_color(1, false, false), [this, &axis_angle_state]() {
                axis_angle_state.combine(erhe::imgui::make_scalar_button(&m_axis.y, -1.0f, 1.0f, "##R.ay"));
            });
            p.add_entry("Z", get_label_color(2, true, false), get_label_color(2, false, false), [this, &axis_angle_state]() {
                axis_angle_state.combine(erhe::imgui::make_scalar_button(&m_axis.z, -1.0f, 1.0f, "##R.az"));
            });
            break;
        }
        case Representation::e_count:
        default: break;
    }
}

auto Rotation_inspector::get_matrix() const -> mat4
{
    return m_matrix;
}

auto Rotation_inspector::get_quaternion() const -> quat
{
    return glm::normalize(m_quaternion);
}

auto Rotation_inspector::get_euler_value(const std::size_t i) const -> float
{
    return m_euler_angles[i];
}

auto Rotation_inspector::get_representation() const -> Representation
{
    return m_representation;
}

auto Rotation_inspector::get_euler_order() const -> Euler_angle_order
{
    return m_euler_angle_order;
}

auto Rotation_inspector::get_euler_axis(const std::size_t i) const -> std::size_t
{
    std::size_t index = static_cast<unsigned int>(m_euler_angle_order);
    const char c = c_euler_strings[index][i];
    switch (c) {
        case 'X': return 0;
        case 'Y': return 1;
        case 'Z': return 2;
        default:  return 0;
    }
}

}
