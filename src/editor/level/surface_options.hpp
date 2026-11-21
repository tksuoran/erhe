#pragma once

#include "config/generated/surface_snap_rest.hpp"
#include "config/generated/surface_support.hpp"

#include <glm/glm.hpp>

#include <string>
#include <string_view>

// The options of snap to surface (doc/editor/level_tools.md "Surface placement").
namespace editor {

inline constexpr float     c_default_surface_max_distance      = 1.0f; // the surface settings' distance, either way
inline constexpr float     c_default_surface_forward_distance  = 1.0f; // a kit layout's (U51)
inline constexpr float     c_default_surface_backward_distance = 0.0f;
inline constexpr glm::vec3 c_default_surface_direction{0.0f, -1.0f, 0.0f};

enum class Surface_snap : unsigned int {
    off = 0,
    on      // the base moves along the direction to the nearest place where it rests and the piece fits (R5.11, U23, U51)
};

enum class Surface_orient : unsigned int {
    off = 0,
    on      // the up axis takes the surface normal, the heading is kept (R5.11)
};

// S2.7: whether a snap of a kit piece onto a support makes the piece its child (doc/editor/level_tools.md "Surface attachments").
enum class Surface_attach : unsigned int {
    off = 0,
    on
};

class Surface_options
{
public:
    Surface_snap   snap     {Surface_snap::off};
    Surface_orient orient   {Surface_orient::off};
    Surface_snap_rest rest  {Surface_snap_rest::leading_face}; // U57, U60: what snap rests with orient off; orient ignores it
    glm::vec3      direction{c_default_surface_direction}; // unit length
    float          forward_distance {c_default_surface_forward_distance};  // meters from the start base along the direction
    float          backward_distance{c_default_surface_backward_distance}; // meters from the start base against it
    Surface_support support {Surface_support::five_lines}; // U52: kit layouts' base lines rule; the others keep five_lines
    Surface_attach attach   {Surface_attach::off};    // read by the callers that attach, never by the search
};

// The surface settings' search (Brush tool, Transform tool, the MCP placement tools): max_distance either way.
[[nodiscard]] auto make_surface_options(Surface_snap snap, Surface_orient orient, const glm::vec3& direction, float max_distance) -> Surface_options;

[[nodiscard]] auto any_surface_option(const Surface_options& options) -> bool;

// Empty when the distance is a finite number >= 0, else why not, naming the value.
[[nodiscard]] auto check_surface_max_distance(float distance, std::string_view name = "surface max distance") -> std::string;

// Empty when the direction is finite and not zero (it is normalized on use), else why not.
[[nodiscard]] auto check_surface_direction(const glm::vec3& direction) -> std::string;

enum class Camera_ray_projection : unsigned int {
    perspective = 0,
    orthographic
};

// U51 camera-ray drags: the unit direction from the camera position through the anchor (perspective), or the view
// direction (orthographic); false when the anchor is at the camera position.
[[nodiscard]] auto get_camera_ray_direction(
    Camera_ray_projection projection,
    const glm::vec3&      camera_position,
    const glm::vec3&      view_direction,
    const glm::vec3&      anchor,
    glm::vec3&            out_direction
) -> bool;

// The axis rule of Transform tool drags (U51): a handle axis within about 2.6 degrees of the search direction, either sign.
[[nodiscard]] auto is_axis_along_direction(const glm::vec3& axis, const glm::vec3& direction) -> bool;

} // namespace editor
