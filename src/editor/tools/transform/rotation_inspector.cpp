#include "tools/transform/rotation_inspector.hpp"
#include "windows/property_editor.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <glm/gtx/euler_angles.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

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
    m_matrix     = m;
    m_quaternion = quat_cast(m);
    update_euler_angles_from_matrix();
    update_axis_angle_from_quaternion();
}

void Rotation_inspector::set_quaternion(const quat& q)
{
    m_quaternion = q;
    m_matrix     = mat3_cast(q);
    update_euler_angles_from_matrix();
    update_axis_angle_from_quaternion();
}

void Rotation_inspector::set_axis_angle(const glm::vec3 axis, const float angle)
{
    m_axis       = axis;
    m_angle      = angle;
    m_quaternion = glm::angleAxis(angle, axis);
    m_matrix     = mat3_cast(m_quaternion);
    update_euler_angles_from_matrix();
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

auto Rotation_inspector::gimbal_lock_warning() const -> float
{
    using namespace std;
    if (is_proper(m_euler_angle_order)) {
        const float modulo   = fmodf(m_euler_angles[1], glm::pi<float>());
        const float distance = std::min(
            std::abs(modulo),
            std::abs(modulo - glm::pi<float>())
        );
        return (distance < 0.15) ? erhe::math::remap(distance, 0.15f, 0.0f, 0.0f, 1.0f) : 0.0f;
    } else {
        const float modulo   = fmodf(m_euler_angles[1] + glm::half_pi<float>(), glm::pi<float>());
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

void Rotation_inspector::update_euler_angles_from_matrix()
{
    switch (m_euler_angle_order) {
        case Euler_angle_order::e_xyx: glm::extractEulerAngleXYX(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_xyz: glm::extractEulerAngleXYZ(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_xzx: glm::extractEulerAngleXZX(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_xzy: glm::extractEulerAngleXZY(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yxy: glm::extractEulerAngleYXY(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yxz: glm::extractEulerAngleYXZ(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yzx: glm::extractEulerAngleYZX(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yzy: glm::extractEulerAngleYZY(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zxy: glm::extractEulerAngleZXY(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zxz: glm::extractEulerAngleZXZ(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zyx: glm::extractEulerAngleZYX(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zyz: glm::extractEulerAngleZYZ(m_matrix, m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        default: break;
    }
}

void Rotation_inspector::update_matrix_and_quaternion_from_euler_angles()
{
    switch (m_euler_angle_order) {
        case Euler_angle_order::e_xyx: m_matrix = glm::eulerAngleXYX(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_xyz: m_matrix = glm::eulerAngleXYZ(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_xzx: m_matrix = glm::eulerAngleXZX(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_xzy: m_matrix = glm::eulerAngleXZY(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yxy: m_matrix = glm::eulerAngleYXY(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yxz: m_matrix = glm::eulerAngleYXZ(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yzx: m_matrix = glm::eulerAngleYZX(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_yzy: m_matrix = glm::eulerAngleYZY(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zxy: m_matrix = glm::eulerAngleZXY(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zxz: m_matrix = glm::eulerAngleZXZ(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zyx: m_matrix = glm::eulerAngleZYX(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        case Euler_angle_order::e_zyz: m_matrix = glm::eulerAngleZYZ(m_euler_angles[0], m_euler_angles[1], m_euler_angles[2]); break;
        default: break;
    }
    m_quaternion = quat_cast(m_matrix);
    //m_quaternion = glm::normalize(m_quaternion);
}

void Rotation_inspector::update_from_axis_angle()
{
    set_axis_angle(m_axis, m_angle);
}

void Rotation_inspector::update_from_quaternion()
{
    m_matrix= mat3_cast(normalize(m_quaternion));
    update_euler_angles_from_matrix();
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
    // ImGui::TextUnformatted(m_active ? "Active" : "Not active");
    
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
                const glm::vec3 col0 = m_matrix[0];
                const glm::vec3 col1 = m_matrix[1];
                const glm::vec3 col2 = m_matrix[2];
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
            p.add_entry("W", get_label_color(3, true, matches_gizmo), get_label_color(3, false, matches_gizmo), [this, &quaternion_state]() {
                quaternion_state.combine(
                    erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qw")
                );
            });
            p.add_entry("X", get_label_color(0, true, matches_gizmo), get_label_color(0, false, matches_gizmo), [this, &quaternion_state]() {
                quaternion_state.combine(
                    erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qx")
                );
            });
            p.add_entry("Y", get_label_color(1, true, matches_gizmo), get_label_color(1, false, matches_gizmo), [this, &quaternion_state]() {
                quaternion_state.combine(
                    erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qy")
                );
            });
            p.add_entry("Z", get_label_color(2, true, matches_gizmo), get_label_color(2, false, matches_gizmo), [this, &quaternion_state]() {
                quaternion_state.combine(
                    erhe::imgui::make_scalar_button(&m_quaternion.w, -1.0f, 1.0f, "##R.qz")
                );
            });
            break;
        }

        case Representation::e_euler_angles: {
            const std::size_t a             = get_euler_axis(0);
            const std::size_t b             = get_euler_axis(1);
            const std::size_t c             = get_euler_axis(2);
            const char*       axis_labels[] = {"X", "Y", "Z"};

            p.add_entry(axis_labels[a], get_label_color(a, true, matches_gizmo), get_label_color(a, false, matches_gizmo), [this, &euler_state]() {
                const float warn = gimbal_lock_warning();
                if (warn > 0.0f) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 1.0f - warn, 0.0f, 1.0f});}
                euler_state.combine(erhe::imgui::make_angle_button(m_euler_angles[0], -10.0f * glm::pi<float>(), 10.0f * glm::pi<float>(), "##R.0"));
                if (warn > 0.0f) { 
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("Gimbal Lock");
                        ImGui::EndTooltip();
                    }
                    ImGui::PopStyleColor(1);
                }
            });
            p.add_entry(axis_labels[b], get_label_color(b, true, matches_gizmo), get_label_color(b, false, matches_gizmo), [this, &euler_state]() {
                const float warn = gimbal_lock_warning();
                if (warn > 0.0f) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 1.0f - warn, 0.0f, 1.0f});}
                euler_state.combine(erhe::imgui::make_angle_button(m_euler_angles[1], -10.0f * glm::half_pi<float>(), 10.0f * glm::half_pi<float>(), "##R.1"));
                if (warn > 0.0f) { 
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("Gimbal Lock");
                        ImGui::EndTooltip();
                    }
                    ImGui::PopStyleColor(1);
                }
            });
            p.add_entry(axis_labels[c], get_label_color(c, true, matches_gizmo), get_label_color(c, false, matches_gizmo), [this, &euler_state]() {
                const float warn = gimbal_lock_warning();
                if (warn > 0.0f) { ImGui::BeginDisabled(); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 1.0f - warn, 0.0f, 1.0f});}
                euler_state.combine(erhe::imgui::make_angle_button(m_euler_angles[2], -10.0f * glm::pi<float>(), 10.0f * glm::pi<float>(), "##R.2"));
                if (warn > 0.0f) { 
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("Gimbal Lock");
                        ImGui::EndTooltip();
                    }
                    ImGui::PopStyleColor(1);
                }
            });
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

auto Rotation_inspector::get_matrix() -> mat4
{
    return m_matrix;
}

auto Rotation_inspector::get_quaternion() -> quat
{
    return glm::normalize(m_quaternion);
}

auto Rotation_inspector::get_euler_value(const std::size_t i) const -> float
{
    return m_euler_angles[i];
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

auto Rotation_inspector::get_label_color(
    const std::size_t i,
    const bool        text,
    const bool        matches_gizmo
) const -> uint32_t
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

} // namespace editor
