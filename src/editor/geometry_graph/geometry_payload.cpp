#include "geometry_graph/geometry_payload.hpp"

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_geometry/geometry.hpp"

namespace editor {

auto Geometry_payload::has_value() const -> bool
{
    return !std::holds_alternative<std::monostate>(value);
}

auto Geometry_payload::get_geometry() const -> std::shared_ptr<erhe::geometry::Geometry>
{
    const std::shared_ptr<erhe::geometry::Geometry>* geometry = std::get_if<std::shared_ptr<erhe::geometry::Geometry>>(&value);
    return (geometry != nullptr) ? *geometry : std::shared_ptr<erhe::geometry::Geometry>{};
}

auto Geometry_payload::get_float(float fallback) const -> float
{
    const float* float_value = std::get_if<float>(&value);
    return (float_value != nullptr) ? *float_value : fallback;
}

auto Geometry_payload::get_int(int fallback) const -> int
{
    const int* int_value = std::get_if<int>(&value);
    return (int_value != nullptr) ? *int_value : fallback;
}

auto Geometry_payload::get_bool(bool fallback) const -> bool
{
    const bool* bool_value = std::get_if<bool>(&value);
    return (bool_value != nullptr) ? *bool_value : fallback;
}

auto Geometry_payload::get_vec3(const glm::vec3& fallback) const -> glm::vec3
{
    const glm::vec3* vec3_value = std::get_if<glm::vec3>(&value);
    return (vec3_value != nullptr) ? *vec3_value : fallback;
}

auto Geometry_payload::get_vec4(const glm::vec4& fallback) const -> glm::vec4
{
    const glm::vec4* vec4_value = std::get_if<glm::vec4>(&value);
    return (vec4_value != nullptr) ? *vec4_value : fallback;
}

auto Geometry_payload::get_mat4(const glm::mat4& fallback) const -> glm::mat4
{
    const glm::mat4* mat4_value = std::get_if<glm::mat4>(&value);
    return (mat4_value != nullptr) ? *mat4_value : fallback;
}

auto Geometry_payload::get_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    const std::shared_ptr<erhe::primitive::Material>* material = std::get_if<std::shared_ptr<erhe::primitive::Material>>(&value);
    return (material != nullptr) ? *material : std::shared_ptr<erhe::primitive::Material>{};
}

auto Geometry_payload::get_points() const -> std::shared_ptr<Point_cloud>
{
    const std::shared_ptr<Point_cloud>* points = std::get_if<std::shared_ptr<Point_cloud>>(&value);
    return (points != nullptr) ? *points : std::shared_ptr<Point_cloud>{};
}

auto Geometry_payload::get_instances() const -> std::shared_ptr<Geometry_instances>
{
    const std::shared_ptr<Geometry_instances>* instances = std::get_if<std::shared_ptr<Geometry_instances>>(&value);
    return (instances != nullptr) ? *instances : std::shared_ptr<Geometry_instances>{};
}

auto Geometry_instances::instance_count() const -> std::size_t
{
    std::size_t count = 0;
    for (const Entry& entry : entries) {
        count += entry.transforms.size();
    }
    return count;
}

auto Geometry_payload::operator+=(const Geometry_payload& rhs) -> Geometry_payload&
{
    if (!rhs.has_value()) {
        return *this;
    }
    if (!has_value()) {
        value = rhs.value;
        return *this;
    }

    if (std::holds_alternative<std::shared_ptr<erhe::geometry::Geometry>>(value) &&
        std::holds_alternative<std::shared_ptr<erhe::geometry::Geometry>>(rhs.value)
    ) {
        // Merge into a new geometry; neither input is modified, so upstream
        // nodes sharing these geometries are unaffected.
        const std::shared_ptr<erhe::geometry::Geometry> lhs_geometry = get_geometry();
        const std::shared_ptr<erhe::geometry::Geometry> rhs_geometry = rhs.get_geometry();
        if (!lhs_geometry) {
            value = rhs.value;
            return *this;
        }
        if (!rhs_geometry) {
            return *this;
        }
        std::shared_ptr<erhe::geometry::Geometry> merged = std::make_shared<erhe::geometry::Geometry>("merged");
        GEO::mat4f identity;
        identity.load_identity();
        merged->merge_with_transform(*lhs_geometry.get(), identity);
        merged->merge_with_transform(*rhs_geometry.get(), identity);
        // merge_with_transform appends raw mesh data only; every payload
        // geometry must be graph-processed (connectivity + edges present),
        // or downstream operations reading e.g. get_vertex_corners() index
        // empty tables and abort.
        process_for_graph(*merged.get());
        value = merged;
        return *this;
    }

    if (std::holds_alternative<std::shared_ptr<Point_cloud>>(value) &&
        std::holds_alternative<std::shared_ptr<Point_cloud>>(rhs.value)
    ) {
        // Concatenate into a new point cloud; neither input is modified.
        const std::shared_ptr<Point_cloud> lhs_points = get_points();
        const std::shared_ptr<Point_cloud> rhs_points = rhs.get_points();
        if (!lhs_points) {
            value = rhs.value;
            return *this;
        }
        if (!rhs_points) {
            return *this;
        }
        std::shared_ptr<Point_cloud> combined = std::make_shared<Point_cloud>();
        combined->positions = lhs_points->positions;
        combined->positions.insert(combined->positions.end(), rhs_points->positions.begin(), rhs_points->positions.end());
        // Normals stay parallel to positions only when both sides carry them.
        if (!lhs_points->normals.empty() && !rhs_points->normals.empty()) {
            combined->normals = lhs_points->normals;
            combined->normals.insert(combined->normals.end(), rhs_points->normals.begin(), rhs_points->normals.end());
        }
        value = combined;
        return *this;
    }

    if (std::holds_alternative<std::shared_ptr<Geometry_instances>>(value) &&
        std::holds_alternative<std::shared_ptr<Geometry_instances>>(rhs.value)
    ) {
        // Concatenate entry lists into a new instance set; neither input
        // is modified. The referenced geometries stay shared.
        const std::shared_ptr<Geometry_instances> lhs_instances = get_instances();
        const std::shared_ptr<Geometry_instances> rhs_instances = rhs.get_instances();
        if (!lhs_instances) {
            value = rhs.value;
            return *this;
        }
        if (!rhs_instances) {
            return *this;
        }
        std::shared_ptr<Geometry_instances> combined = std::make_shared<Geometry_instances>();
        combined->entries = lhs_instances->entries;
        combined->entries.insert(combined->entries.end(), rhs_instances->entries.begin(), rhs_instances->entries.end());
        value = combined;
        return *this;
    }

    if (std::holds_alternative<float>(value) && std::holds_alternative<float>(rhs.value)) {
        value = std::get<float>(value) + std::get<float>(rhs.value);
        return *this;
    }
    if (std::holds_alternative<int>(value) && std::holds_alternative<int>(rhs.value)) {
        value = std::get<int>(value) + std::get<int>(rhs.value);
        return *this;
    }
    if (std::holds_alternative<glm::vec3>(value) && std::holds_alternative<glm::vec3>(rhs.value)) {
        value = std::get<glm::vec3>(value) + std::get<glm::vec3>(rhs.value);
        return *this;
    }
    if (std::holds_alternative<glm::vec4>(value) && std::holds_alternative<glm::vec4>(rhs.value)) {
        value = std::get<glm::vec4>(value) + std::get<glm::vec4>(rhs.value);
        return *this;
    }

    // Other types (bool, mat4, material): keep the first connected value.
    return *this;
}

}
