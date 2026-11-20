#pragma once

#include "level/kit_connector.hpp"
#include "level/kit_standard_property.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GEO { class Mesh; }
namespace erhe::math { class Curve_path; class Curve_path_table; }

// Scene-free polygon floor of a floor kit layout (doc/plans/level_kit_connections.md C8, design K3).

namespace editor {

// The values of a floor layout (meters); the defaults are basic.floor_slab's thickness and architecture.wall's.
class Floor_values
{
public:
    float thickness     {0.2f};  // the slab's top is at this height above the node's origin
    float wall_thickness{0.25f}; // the inset of the wall connectors and the edge lengths the floor supplies
    float room_height   {3.0f};  // the room height the floor supplies to its walls
    float rail_inset    {0.1f};  // C10.2: the rail connectors' line, this far inside each edge

    [[nodiscard]] auto operator==(const Floor_values& other) const -> bool = default;
};

// The least wall length an edge supplies (meters): the walls' length minimum.
inline constexpr float c_floor_min_wall_length = 0.1f;

// The shortest edge an outline keeps (meters).
inline constexpr float c_floor_min_edge = 0.01f;

// The most edges (corner points) a floor has: one wall connector per edge, within the shape connector bound.
inline constexpr std::size_t c_floor_max_edges = 64;

// Part of a generated floor's source text: raised when the generator makes another mesh from the same outline and values.
inline constexpr int c_floor_generator_version = 1;

// Returns the refusal naming the value out of range.
[[nodiscard]] auto check_floor_values(const Floor_values& values) -> std::string;

// The outline of a floor layout's curve: the start of each span, in order; refuses an open curve, a curved span (a floor's
// points are corners) and a corner off the node's plane (y != 0). The one reader of the outline (C8.1).
[[nodiscard]] auto read_floor_outline(const erhe::math::Curve_path_table& table, std::vector<glm::vec2>& out_outline) -> std::string;

// Twice the signed area in plan (x, z); negative for an outline wound counter-clockwise seen from above.
[[nodiscard]] auto get_floor_outline_area2(std::span<const glm::vec2> outline) -> float;

// C8.3: at least three corners, at most c_floor_max_edges, every edge at least c_floor_min_edge, no two edges crossing,
// a non-zero area. Empty when valid; otherwise the refusal naming the edge or corner.
[[nodiscard]] auto check_floor_outline(std::span<const glm::vec2> outline) -> std::string;

// C8.3: as check_floor_outline(), and the winding of the outline is the winding of the outline before (no corner turned
// inside out).
[[nodiscard]] auto check_floor_outline_change(std::span<const glm::vec2> before, std::span<const glm::vec2> after) -> std::string;

// The closed path of a floor: one corner point per outline corner at height 0, in order.
[[nodiscard]] auto make_floor_path(std::span<const glm::vec2> outline) -> erhe::math::Curve_path;

// The rectangle of a new floor (C8.2): centred on the origin, wound counter-clockwise seen from above.
void make_floor_rectangle(float width, float depth, std::vector<glm::vec2>& out_outline);

// The slab: the outline extruded from y = 0 up to y = thickness, its caps triangulated by ear clipping; returns the
// refusal of the outline or the values.
[[nodiscard]] auto make_floor_mesh(std::span<const glm::vec2> outline, const Floor_values& values, GEO::Mesh& mesh) -> std::string;

// The length of edge i (from corner i to corner i + 1, the last edge closing the outline).
[[nodiscard]] auto get_floor_edge_length(std::span<const glm::vec2> outline, std::size_t edge) -> float;

// The unit plan direction from the edge toward the inside of the outline.
[[nodiscard]] auto get_floor_edge_inward(std::span<const glm::vec2> outline, std::size_t edge) -> glm::vec2;

// The connector name of edge i: "edge_<i>", a literal.
[[nodiscard]] auto get_floor_edge_connector_name(std::size_t edge) -> const char*;
// The floor connector name of edge i: "floor_<i>", a literal.
[[nodiscard]] auto get_floor_floor_connector_name(std::size_t edge) -> const char*;
// The rail connector name of edge i: "rail_<i>", a literal (C10.2).
[[nodiscard]] auto get_floor_rail_connector_name(std::size_t edge) -> const char*;

// The connector families of a floor, one connector per edge each; they rename apart when the edges renumber (C8.4).
enum class Floor_connector_family : unsigned int
{
    edge = 0, // edge_<i>, wall-floor
    floor,    // floor_<i>, floor
    rail      // rail_<i>, rail
};

class Floor_connector_name
{
public:
    Floor_connector_family family{Floor_connector_family::edge};
    std::size_t            edge  {0}; // edge_count when the name's number is not an edge of the outline
};

// The family and edge of a floor connector name; nullopt for a name of no family.
[[nodiscard]] auto parse_floor_connector_name(std::string_view name, std::size_t edge_count) -> std::optional<Floor_connector_name>;
[[nodiscard]] auto get_floor_connector_name(Floor_connector_family family, std::size_t edge) -> const char*;

// C10.2: the line of edge i moved rail_inset inside, from the crossing with edge i - 1's moved line to the crossing with
// edge i + 1's (where two neighbouring lines are parallel, the corner moved along the inward); length is signed.
class Floor_rail_span
{
public:
    glm::vec2 start {0.0f};
    glm::vec2 end   {0.0f};
    float     length{0.0f};
};
[[nodiscard]] auto get_floor_rail_span(std::span<const glm::vec2> outline, float rail_inset, std::size_t edge) -> Floor_rail_span;

// C2.4, K4.5: one wall-floor connector per edge (edge_<i>), at the edge's midpoint on the top, inset by half the wall
// thickness, facing up, its up axis toward the inside; then one floor connector per edge (floor_<i>), at the edge's
// midpoint on the top, facing out, level; then one rail connector per edge (rail_<i>, a rail host, C10.2), at the middle
// of its rail span on the top, facing up, its up axis toward the inside; into the caller's vector (cleared, capacity kept).
void get_floor_connectors(std::span<const glm::vec2> outline, const Floor_values& values, std::vector<Kit_connector>& out_connectors);

// C3.3, K4.3: what edge i supplies to the wall standing on it, at the floor priority: the edge's length minus the wall
// thickness (so the walls on neighbouring edges meet at their center lines, L9; none below 0.1 m, the walls' least
// length), the wall thickness, the room height and the wall material (resolved by the caller: the slot's default for an
// empty floor_wall_material).
void get_floor_supply(
    std::span<const glm::vec2> outline,
    const Floor_values&        values,
    std::string_view           wall_material,
    std::size_t                edge,
    Kit_supply_list&           out_supply
);

// C10.2: what rail_<i> supplies to its rider at the floor priority: the length of its rail span (none below
// c_floor_min_wall_length), so the riders of neighbouring edges meet where their lines cross.
void get_floor_rail_supply(std::span<const glm::vec2> outline, const Floor_values& values, std::size_t edge, Kit_supply_list& out_supply);

// What the floor supplies over the named connector: get_floor_supply() for edge_<i>, get_floor_rail_supply() for
// rail_<i>, nothing for the others.
void get_floor_connector_supply(
    std::span<const glm::vec2> outline,
    const Floor_values&        values,
    std::string_view           wall_material,
    std::string_view           connector_name,
    Kit_supply_list&           out_supply
);

// The canonical text of a generated floor's source: the generator version, the outline and the values.
[[nodiscard]] auto make_floor_source_text(std::span<const glm::vec2> outline, const Floor_values& values) -> std::string;

} // namespace editor
