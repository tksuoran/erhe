#include "geometry_graph/nodes/transform_node.hpp"

#include "graph_editor/graph_editor_widgets.hpp"

#include "erhe_geometry/geometry.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

Transform_node::Transform_node()
    : Geometry_graph_node{"Transform"}
{
    make_input_pin(Geometry_pin_key::geometry,   "in");
    make_input_pin(Geometry_pin_key::vec3_value, "translation");
    make_input_pin(Geometry_pin_key::vec3_value, "rotation");
    make_input_pin(Geometry_pin_key::vec3_value, "scale");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Transform_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }

    const glm::vec3 translation = get_input(1).get_vec3(m_translation);
    const glm::vec3 scale       = get_input(3).get_vec3(m_scale);

    glm::mat4 transform{1.0f};
    transform = glm::translate(transform, translation);
    if (m_rotation_mode == Rotation_mode::quaternion) {
        // The "rotation" input pin is Euler-only (vec3); quaternion mode
        // rotates by the parameter, normalized so hand-authored values
        // need not be exactly unit length (zero-length falls back to
        // identity).
        const glm::quat rotation{m_rotation_quaternion.w, m_rotation_quaternion.x, m_rotation_quaternion.y, m_rotation_quaternion.z};
        const float length_squared = glm::dot(rotation, rotation);
        if (length_squared > 0.0f) {
            transform = transform * glm::mat4_cast(glm::normalize(rotation));
        }
    } else {
        const glm::vec3 rotation_degrees = get_input(2).get_vec3(m_rotation_degrees);
        transform = glm::rotate(transform, glm::radians(rotation_degrees.z), glm::vec3{0.0f, 0.0f, 1.0f});
        transform = glm::rotate(transform, glm::radians(rotation_degrees.y), glm::vec3{0.0f, 1.0f, 0.0f});
        transform = glm::rotate(transform, glm::radians(rotation_degrees.x), glm::vec3{1.0f, 0.0f, 0.0f});
    }
    transform = glm::scale(transform, scale);

    if (transform == glm::mat4{1.0f}) {
        // Copy-on-write: an identity transform passes the upstream
        // geometry through unchanged instead of copying it. Safe because
        // nodes never mutate shared upstream geometry.
        set_output(0, Geometry_payload{.value = source});
        return;
    }

    std::shared_ptr<erhe::geometry::Geometry> destination = std::make_shared<erhe::geometry::Geometry>("transformed");
    destination->copy_with_transform(*source.get(), erhe::geometry::to_geo_mat4f(transform));
    set_output(0, Geometry_payload{.value = destination});
}

void Transform_node::imgui()
{
    ImGui::TextUnformatted("Translation");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat3("##translation", &m_translation.x, 0.01f)) { mark_dirty(); }
    const char* rotation_mode_names[] = { "Euler (deg)", "Quaternion" };
    int rotation_mode = static_cast<int>(m_rotation_mode);
    if (imgui_enum_combo("rotation mode", rotation_mode, rotation_mode_names, IM_ARRAYSIZE(rotation_mode_names), content_scale())) {
        m_rotation_mode = static_cast<Rotation_mode>(rotation_mode);
        mark_dirty();
    }
    if (m_rotation_mode == Rotation_mode::quaternion) {
        ImGui::TextUnformatted("Rotation (quat xyzw)");
        ImGui::SetNextItemWidth(180.0f * content_scale());
        if (ImGui::DragFloat4("##rotation_quaternion", &m_rotation_quaternion.x, 0.01f)) { mark_dirty(); }
    } else {
        ImGui::TextUnformatted("Rotation (deg)");
        ImGui::SetNextItemWidth(140.0f * content_scale());
        if (ImGui::DragFloat3("##rotation", &m_rotation_degrees.x, 0.1f)) { mark_dirty(); }
    }
    ImGui::TextUnformatted("Scale");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat3("##scale", &m_scale.x, 0.01f)) { mark_dirty(); }
}

void Transform_node::write_parameters(nlohmann::json& out) const
{
    write_vec3(out, "translation", m_translation);
    out["rotation_mode"] = static_cast<int>(m_rotation_mode);
    write_vec3(out, "rotation",            m_rotation_degrees);
    write_vec4(out, "rotation_quaternion", m_rotation_quaternion);
    write_vec3(out, "scale",       m_scale);
}

void Transform_node::read_parameters(const nlohmann::json& in)
{
    m_translation         = read_vec3(in, "translation", m_translation);
    m_rotation_mode       = static_cast<Rotation_mode>(in.value("rotation_mode", static_cast<int>(m_rotation_mode)));
    m_rotation_degrees    = read_vec3(in, "rotation",            m_rotation_degrees);
    m_rotation_quaternion = read_vec4(in, "rotation_quaternion", m_rotation_quaternion);
    m_scale               = read_vec3(in, "scale",       m_scale);
    mark_dirty();
}

}
