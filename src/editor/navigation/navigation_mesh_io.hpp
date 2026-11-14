#pragma once

#include "navigation/navigation_mesh.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace editor {

// The content hash of one primitive's render shape as the build read it; not known when the build had none.
class Navigation_shape_hash
{
public:
    bool                    known{false};
    std::array<uint8_t, 16> bytes{};

    auto operator==(const Navigation_shape_hash& other) const -> bool = default;
};

// One input mesh of the build, named across sessions by its glTF uid.
class Navigation_record_input
{
public:
    std::string                        uid;
    glm::mat4                          root_from_node{1.0f}; // compared bit for bit
    std::vector<Navigation_shape_hash> shape_hashes;         // parallel to the mesh's primitives
};

// One seed of the flood, named across sessions by its node's glTF uid.
class Navigation_record_seed
{
public:
    std::string            uid;
    std::string            name;
    glm::vec3              point{0.0f}; // compared bit for bit
    Navigation_seed_result result;
};

// What the navigation mesh was built and flooded from (doc/editor/navigation.md "Saving").
class Navigation_build_record
{
public:
    float                                seed_search_radius{1.0f}; // the last flood's, which may differ from the build's
    float                                seed_search_height{3.0f};
    std::vector<Navigation_record_seed>  seeds;
    std::vector<Navigation_record_input> input;
};

// The version of the byte form; read_navigation_mesh() refuses every other.
inline constexpr uint32_t c_navigation_mesh_format_version = 2;

// Byte form of the navigation mesh item: header, settings, Detour tiles, polygon arrays and the build record.
[[nodiscard]] auto write_navigation_mesh(const Navigation_mesh& navigation_mesh, const Navigation_build_record& record) -> std::vector<uint8_t>;

// Null with out_error set when the bytes are not a navigation mesh this code reads; the caller logs.
[[nodiscard]] auto read_navigation_mesh(
    std::span<const uint8_t> bytes,
    Navigation_build_record& out_record,
    std::string&             out_error
) -> std::unique_ptr<Navigation_mesh>;

} // namespace editor
