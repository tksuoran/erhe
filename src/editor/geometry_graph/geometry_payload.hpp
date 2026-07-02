#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <variant>

namespace erhe::geometry  { class Geometry; }
namespace erhe::primitive { class Material; }

namespace editor {

// Pin keys used by the geometry graph. erhe::graph::Graph::connect() rejects
// links between pins whose keys differ, so giving each payload type its own
// key makes connections type safe (geometry pins only connect to geometry
// pins, float pins only to float pins, and so on).
class Geometry_pin_key
{
public:
    static constexpr std::size_t geometry    = 1;
    static constexpr std::size_t float_value = 2;
    static constexpr std::size_t int_value   = 3;
    static constexpr std::size_t bool_value  = 4;
    static constexpr std::size_t vec3_value  = 5;
    static constexpr std::size_t vec4_value  = 6;
    static constexpr std::size_t mat4_value  = 7;
    static constexpr std::size_t material    = 8;
};

// Value carried through geometry graph pins.
class Geometry_payload
{
public:
    using Variant = std::variant<
        std::monostate,
        std::shared_ptr<erhe::geometry::Geometry>,
        float,
        int,
        bool,
        glm::vec3,
        glm::vec4,
        glm::mat4,
        std::shared_ptr<erhe::primitive::Material>
    >;

    [[nodiscard]] auto has_value   () const -> bool;
    [[nodiscard]] auto get_geometry() const -> std::shared_ptr<erhe::geometry::Geometry>;
    [[nodiscard]] auto get_float   (float fallback = 0.0f) const -> float;
    [[nodiscard]] auto get_int     (int fallback = 0) const -> int;
    [[nodiscard]] auto get_bool    (bool fallback = false) const -> bool;
    [[nodiscard]] auto get_vec3    (const glm::vec3& fallback = glm::vec3{0.0f}) const -> glm::vec3;
    [[nodiscard]] auto get_vec4    (const glm::vec4& fallback = glm::vec4{0.0f}) const -> glm::vec4;
    [[nodiscard]] auto get_mat4    (const glm::mat4& fallback = glm::mat4{1.0f}) const -> glm::mat4;
    [[nodiscard]] auto get_material() const -> std::shared_ptr<erhe::primitive::Material>;

    // Accumulation used when multiple links feed one input pin:
    // - empty += x adopts x
    // - numeric and vector types add
    // - geometries merge into a newly allocated combined geometry
    // - materials keep the first connected value
    auto operator+=(const Geometry_payload& rhs) -> Geometry_payload&;

    Variant value{};
};

}
