#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

// Standard properties of doc/plans/level_kit_connections.md C2 and their supply over connectors (design K3, K4).

namespace editor {

class Kit_parameter_values;

enum class Kit_standard_property : unsigned int
{
    room_height = 0,
    floor_thickness,
    wall_thickness,
    child_length,
    floor_material,
    wall_material
};

inline constexpr std::size_t c_standard_property_count = 6;

enum class Kit_property_kind : unsigned int
{
    length = 0, // meters, a float parameter
    material    // a material slot
};

[[nodiscard]] auto c_str   (Kit_standard_property property) -> const char*; // "room.height", ...
[[nodiscard]] auto get_kind(Kit_standard_property property) -> Kit_property_kind;
[[nodiscard]] auto parse_standard_property(std::string_view name) -> std::optional<Kit_standard_property>;

// A shape's parameter (a length) or material slot (a material) a standard property maps onto (C2.2).
class Kit_property_mapping
{
public:
    Kit_standard_property property{Kit_standard_property::room_height};
    const char*           target  {""};
};

// One value a piece supplies over a connector (C3.3), with its priority (C4). The material is a resolved name (never empty)
// that the caller keeps alive: a supply list is consumed within the call that fills it.
class Kit_supplied_value
{
public:
    Kit_standard_property property{Kit_standard_property::room_height};
    int                   priority{0};
    float                 length  {0.0f};
    std::string_view      material;
};

// At most one value per property: filling it allocates nothing.
class Kit_supply_list
{
public:
    void clear() { m_count = 0; }
    void add  (const Kit_supplied_value& value);
    [[nodiscard]] auto get () const -> std::span<const Kit_supplied_value> { return std::span<const Kit_supplied_value>{m_values.data(), m_count}; }
    [[nodiscard]] auto find(Kit_standard_property property) const -> const Kit_supplied_value*;

private:
    std::array<Kit_supplied_value, c_standard_property_count> m_values{};
    std::size_t                                               m_count{0};
};

// What a piece's supply reads: its parameter values and the names of its slot materials in slot order.
class Kit_piece_view
{
public:
    const Kit_parameter_values*        values{nullptr};
    std::span<const std::string_view>  slot_materials;
};

// The priorities of the first shapes (C4.4).
inline constexpr int c_floor_supply_priority = 2;
inline constexpr int c_wall_supply_priority  = 1;

} // namespace editor
