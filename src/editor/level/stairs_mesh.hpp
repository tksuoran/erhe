#pragma once

#include "level/stairs_fit.hpp"

#include <cstddef>
#include <string>

namespace GEO { class Mesh; }
namespace erhe::math { class Curve_path_table; }

// Scene-free staircase along a curve (doc/editor/level_tools.md "Stairs layouts", R5.12.13).

namespace editor {

// The plan radius of the curve is at least this times the width, so the inner edge of a tread keeps a third of its depth.
inline constexpr float       c_stairs_min_radius_per_width = 0.75f;
// A turn of the plan tangent above this at a control point is a corner (radians).
inline constexpr float       c_stairs_corner_angle         = 1.0e-3f;
inline constexpr float       c_stairs_max_corner_angle     = 2.8f;
// A corner's landing reaches this far beyond the place its mitre line ends on the inner side (meters).
inline constexpr float       c_stairs_corner_margin        = 0.02f;
inline constexpr std::size_t c_max_stairs_sections         = 4096;
// The edge between two facets sharper than this is hard (degrees).
inline constexpr float       c_stairs_crease_angle_degrees = 20.0f;

// Part of a generated staircase's source text: raised when the generator makes another mesh from the same curve and values.
inline constexpr int         c_stairs_generator_version    = 1;

inline constexpr float c_min_stairs_width     = 0.3f;
inline constexpr float c_max_stairs_width     = 32.0f;
inline constexpr float c_min_stairs_rise      = 0.05f;
inline constexpr float c_max_stairs_rise      = 1.0f;
inline constexpr float c_min_stairs_tread     = 0.1f;
inline constexpr float c_max_stairs_tread     = 4.0f;
inline constexpr float c_min_stairs_thickness = 0.02f;
inline constexpr float c_max_stairs_thickness = 4.0f;
inline constexpr float c_min_stairs_tolerance = 0.001f;
inline constexpr float c_max_stairs_tolerance = 1.0f;

// The values of a stairs layout (meters); the defaults are the step rules of basic.stairs (R3.5).
class Stairs_values
{
public:
    float width    {1.0f};
    float max_rise {0.2f};  // the step rise limit
    float min_tread{0.3f};  // the tread depth of a flight, on the curve
    float thickness{0.2f};  // below the foot of each riser and below a landing
    float tolerance{0.01f}; // the most a curved side edge's chord leaves its curve

    [[nodiscard]] auto operator==(const Stairs_values& other) const -> bool = default;
};

// Returns the refusal naming the value out of range.
[[nodiscard]] auto check_stairs_values(const Stairs_values& values) -> std::string;

class Stairs_report
{
public:
    Stairs_fit  fit;
    std::size_t section_count{0}; // the cross-sections the mesh is made of

    void clear();
};

// table: the curve in the stairs' own frame, y up. out_mesh: null for the fit alone, else an empty mesh the closed solid
// is written to (its facets carry slot 0). Returns the refusal naming the place; the mesh is then untouched.
[[nodiscard]] auto make_stairs(
    const erhe::math::Curve_path_table& table,
    const Stairs_values&                values,
    Stairs_report&                      out_report,
    GEO::Mesh*                          out_mesh
) -> std::string;

} // namespace editor
