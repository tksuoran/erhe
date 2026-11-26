#pragma once

#include "level/kit_connector.hpp"
#include "level/kit_shape.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Kit_random;

// What stairs values produce (R3.5): steps are equal, each at most the step rise limit high.
class Kit_stairs_layout
{
public:
    int   step_count   {0};
    float step_rise    {0.0f};
    float tread_depth  {0.0f};
    float run          {0.0f};
    float slope_degrees{0.0f};
};

// Tread depth is this times the step rise: a slope of about 33.7 degrees.
constexpr float kit_stairs_tread_per_rise = 1.5f;
constexpr int   kit_stairs_max_steps      = 128;

[[nodiscard]] auto get_stairs_layout(const Kit_parameter_values& values) -> Kit_stairs_layout;

// R2.10 for stairs of this step rise, tread depth and width, named in the message: empty when the agent walks them.
[[nodiscard]] auto check_stairs_walkability(std::string_view name, float step_rise, float tread_depth, float width, const Kit_agent& agent) -> std::string;

// Clear width of the arch opening at the given height above its base, 0 above the crown.
[[nodiscard]] auto get_arch_clear_width(const Kit_parameter_values& values, float height) -> float;

// Empty when the default agent passes the opening (R2.10); otherwise the message naming the shape and the shortfall.
[[nodiscard]] auto check_kit_opening(const Kit_shape_definition& shape, float width, float height, const Kit_agent& agent) -> std::string;

// The head of an arch (Q3 of doc/plans/level_kit_more_shapes.md).
enum class Kit_arch_head : unsigned int {
    round = 0, // half an ellipse
    pointed    // two arcs with their centers on the springing line
};

// The intrados from the right foot (half_span, 0) over the crown (0, rise) to the left foot: segments + 1 points, the
// left half the right half's mirror image; a pointed head takes an even count (an odd one is one less). With a random
// stream every point but the feet moves along its radius from the origin by up to jitter of it.
[[nodiscard]] auto make_kit_arch_head_points(
    Kit_arch_head head,
    float         half_span,
    float         rise,
    int           segments,
    float         jitter = 0.0f,
    Kit_random*   random = nullptr
) -> std::vector<glm::vec2>;

// Point i of n on the unit circle, (x, z) from +X toward -Z; mirror images are equal to the bit and axis points exact.
[[nodiscard]] auto get_kit_circle_point(int i, int n) -> glm::vec2;

// U53: a toothed wheel's boundary connectors besides its mesh connector on +X, at quarter turns 1 - 3.
inline constexpr std::array<const char*, 3> c_kit_gear_boundary_names{"mesh_2", "mesh_3", "mesh_4"};

// The unit direction (x, z) of the boundary connector at the quarter turn: at the tooth at or before it, so that a
// tooth stands a quarter pitch before it as before the mesh connector (doc/editor/level_kit_shapes.md "Machine kit").
[[nodiscard]] auto get_kit_gear_boundary_direction(int quarter, int teeth) -> glm::vec2;

void add_basic_shapes       (std::vector<Kit_shape_definition>& shapes);
void add_architecture_shapes(std::vector<Kit_shape_definition>& shapes);
void add_nature_shapes      (std::vector<Kit_shape_definition>& shapes);
// The shapes of the build-order lines of doc/plans/level_kit_more_shapes.md, one file per batch.
void add_build_13_16_shapes (std::vector<Kit_shape_definition>& shapes);
void add_build_17_20_shapes (std::vector<Kit_shape_definition>& shapes);
void add_build_21_24_shapes (std::vector<Kit_shape_definition>& shapes);
// The shapes of the build-order lines of doc/plans/level_kit_extra_shapes.md, one file per batch.
void add_extra_01_08_shapes (std::vector<Kit_shape_definition>& shapes);
void add_extra_09_16_shapes (std::vector<Kit_shape_definition>& shapes);
void add_extra_17_24_shapes (std::vector<Kit_shape_definition>& shapes);
void add_extra_25_32_shapes (std::vector<Kit_shape_definition>& shapes);
void add_extra_33_36_shapes (std::vector<Kit_shape_definition>& shapes);
void add_machine_01_08_shapes(std::vector<Kit_shape_definition>& shapes);
void add_machine_09_18_shapes(std::vector<Kit_shape_definition>& shapes);
// Called by add_nature_shapes(): the terrain (cliff, shore, water, dune, ground patch), the rocks and the plants.
void add_nature_terrain_shapes(std::vector<Kit_shape_definition>& shapes);
void add_nature_rock_shapes   (std::vector<Kit_shape_definition>& shapes);
void add_nature_plant_shapes  (std::vector<Kit_shape_definition>& shapes);
// Called by add_architecture_shapes(): the cultural modules of doc/plans/level_kit_more_shapes.md.
void add_classical_shapes     (std::vector<Kit_shape_definition>& shapes);
void add_japanese_shapes      (std::vector<Kit_shape_definition>& shapes);
void add_medieval_shapes      (std::vector<Kit_shape_definition>& shapes);
void add_roof_shapes          (std::vector<Kit_shape_definition>& shapes);

// Which build a generator makes: the current one, or the former one an "<id>_old" shape keeps for comparison.
enum class Kit_build : unsigned int { current = 0, former };

// Appends a copy of the shape of the id with the generator of its former build, id "<id>_old" and display name
// "<name> (old)", for a side by side comparison (doc/editor/level_kit_shapes.md "Former builds").
void add_former_build(std::vector<Kit_shape_definition>& shapes, std::string_view id, Kit_generator former_generator);

// Parameter builders shared by the kit files.
// The level of detail every shape takes as its last parameter; Kit_shape_library adds it.
[[nodiscard]] auto make_detail_parameter() -> Kit_parameter;
[[nodiscard]] auto make_length_parameter(
    const char*       name,
    const char*       description,
    float             default_value,
    Kit_default_scale default_scale,
    float             min_value,
    float             max_value
) -> Kit_parameter;
[[nodiscard]] auto make_ratio_parameter   (const char* name, const char* description, float default_value) -> Kit_parameter;
[[nodiscard]] auto make_angle_parameter   (const char* name, const char* description, float default_degrees, float min_degrees, float max_degrees) -> Kit_parameter;
[[nodiscard]] auto make_count_parameter   (const char* name, const char* description, int default_value, int min_value, int max_value) -> Kit_parameter;
[[nodiscard]] auto make_seed_parameter    () -> Kit_parameter;
[[nodiscard]] auto make_enum_parameter    (const char* name, const char* description, std::vector<std::string> value_names, int default_index) -> Kit_parameter;
// The shading parameter (c_kit_shading_parameter, get_kit_shading()): faceted, or the shape's own normal style.
[[nodiscard]] auto make_shading_parameter (Kit_shading default_shading) -> Kit_parameter;
[[nodiscard]] auto all_sides              () -> std::vector<Kit_side>;

// Connector builders shared by the kit files (D1.12): "base" at the origin facing -Y, then "top" at top_y facing +Y,
// both with up +Z; a side connector at the given point, with up +Y. A shape lists first the connector it attaches by.
void add_base_and_top_connectors(std::vector<Kit_connector>& out_connectors, float top_y);
// K1.5: a wall that runs along X: "start" and "end" join walls and generic sides, "base" stands only on a floor (L15),
// "top" hosts riders (rail and generic, C10.2).
void type_wall_run_connectors(std::vector<Kit_connector>& out_connectors);
// C10.3: a rider's base, at the origin facing -Y with up +Z, accepting rail and generic; a no-twist join runs it along
// the host's X (a wall's run, a floor edge).
void type_rider_base_connector(std::vector<Kit_connector>& out_connectors);
// C10.4: the count of repeats along a run of the length whose spacing is nearest to the spacing, 1 to max_count.
[[nodiscard]] auto get_kit_repeat_count(float length, float spacing, int max_count) -> int;
// C2.2, C3.3: the standard property mappings of a wall run shape and its supply over start and end (thickness, material,
// height at the wall priority) and over top (length, thickness: C10.2); the wall slot is the shape's slot of that name;
// thickness_parameter null: no thickness.
void set_wall_run_flow(Kit_shape_definition& shape, const char* thickness_parameter);
void add_side_connector         (std::vector<Kit_connector>& out_connectors, const char* name, const glm::vec3& position, const glm::vec3& outward);

// Derive ranges (D1.8): a float's range and step are in the parameter's default scale; an enum derives over all its values.
[[nodiscard]] auto derive_float        (Kit_parameter parameter, float min_value, float max_value, float step) -> Kit_parameter;
[[nodiscard]] auto derive_int          (Kit_parameter parameter, int min_value, int max_value) -> Kit_parameter;
[[nodiscard]] auto derive_enum         (Kit_parameter parameter) -> Kit_parameter;
// Puts a derivable parameter in its shape's default mask.
[[nodiscard]] auto from_seed_by_default(Kit_parameter parameter) -> Kit_parameter;

} // namespace editor
