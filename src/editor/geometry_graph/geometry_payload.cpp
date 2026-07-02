#include "geometry_graph/geometry_payload.hpp"

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
        value = merged;
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
