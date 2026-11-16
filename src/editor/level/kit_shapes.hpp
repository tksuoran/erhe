#pragma once

#include "level/kit_connector.hpp"
#include "level/kit_shape.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace editor {

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

// A recreated shape's id is its original's plus this, its display name its original's plus the mark.
constexpr const char* c_kit_recreated_id_suffix   = ".v2";
constexpr const char* c_kit_recreated_name_suffix = " (F)";

// The original's definition with the recreated id, name and `recreates`, the given generator and triangle budget.
[[nodiscard]] auto make_recreated_shape(
    const std::vector<Kit_shape_definition>& shapes,
    std::string_view                         original_id,
    Kit_generator                            generator,
    unsigned int                             triangle_budget
) -> Kit_shape_definition;

void add_basic_shapes       (std::vector<Kit_shape_definition>& shapes);
// The recreated shapes (doc/editor/level_tools.md "Recreated shapes"); each call follows its originals' call.
void add_basic_shapes_v2    (std::vector<Kit_shape_definition>& shapes);
void add_architecture_shapes(std::vector<Kit_shape_definition>& shapes);
void add_nature_shapes      (std::vector<Kit_shape_definition>& shapes);
// Called by add_nature_shapes(): the terrain (cliff, shore, water, dune, ground patch), the rocks and the plants.
void add_nature_terrain_shapes(std::vector<Kit_shape_definition>& shapes);
void add_nature_rock_shapes   (std::vector<Kit_shape_definition>& shapes);
void add_nature_plant_shapes  (std::vector<Kit_shape_definition>& shapes);
// Called by add_architecture_shapes(): the cultural modules of doc/plans/level_kit_more_shapes.md.
void add_classical_shapes     (std::vector<Kit_shape_definition>& shapes);
void add_japanese_shapes      (std::vector<Kit_shape_definition>& shapes);
void add_medieval_shapes      (std::vector<Kit_shape_definition>& shapes);
void add_roof_shapes          (std::vector<Kit_shape_definition>& shapes);

// Parameter builders shared by the kit files.
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
[[nodiscard]] auto all_sides              () -> std::vector<Kit_side>;

// Connector builders shared by the kit files (D1.12): "base" at the origin facing -Y, then "top" at top_y facing +Y,
// both with up +Z; a side connector at the given point, with up +Y. A shape lists first the connector it attaches by.
void add_base_and_top_connectors(std::vector<Kit_connector>& out_connectors, float top_y);
void add_side_connector         (std::vector<Kit_connector>& out_connectors, const char* name, const glm::vec3& position, const glm::vec3& outward);

// Derive ranges (D1.8): a float's range and step are in the parameter's default scale; an enum derives over all its values.
[[nodiscard]] auto derive_float        (Kit_parameter parameter, float min_value, float max_value, float step) -> Kit_parameter;
[[nodiscard]] auto derive_int          (Kit_parameter parameter, int min_value, int max_value) -> Kit_parameter;
[[nodiscard]] auto derive_enum         (Kit_parameter parameter) -> Kit_parameter;
// Puts a derivable parameter in its shape's default mask.
[[nodiscard]] auto from_seed_by_default(Kit_parameter parameter) -> Kit_parameter;

} // namespace editor
