#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <string>
#include <vector>

// Scene-free pattern expansion of kit layout nodes (doc/editor/level_tools.md "Pattern expansion", D3.7).

namespace editor {

enum class Repeat_pattern_kind : unsigned int {
    line = 0,
    grid,
    ring,
    polyline
};

[[nodiscard]] auto c_str(Repeat_pattern_kind kind) -> const char*;

enum class Repeat_ring_facing : unsigned int {
    outward = 0,   // each element turns with its angle about the ring's center
    start_rotation // each element keeps the start rotation
};

inline constexpr std::size_t c_max_repeat_count = 1024;

class Repeat_pose
{
public:
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

// The fields of the kind in use count; lengths in meters, axes in the start frame unless stated.
class Repeat_pattern
{
public:
    Repeat_pattern_kind    kind        {Repeat_pattern_kind::line};
    int                    count       {3};                    // line, ring
    glm::vec3              spacing     {1.0f, 0.0f, 0.0f};     // line: the step from one element to the next
    int                    count_x     {3};                    // grid
    int                    count_z     {3};                    // grid
    float                  spacing_x   {1.0f};                 // grid
    float                  spacing_z   {1.0f};                 // grid
    float                  radius      {4.0f};                 // ring
    float                  arc_degrees {360.0f};               // ring: 360 is a full ring
    Repeat_ring_facing     facing      {Repeat_ring_facing::outward};
    std::vector<glm::vec3> points;                             // polyline: after the start position, scene root space
    float                  path_spacing{1.0f};                 // polyline: arc length between elements
};

// The poses of the run, element 0 at the start; cleared and filled. Returns the refusal (empty when expanded),
// which leaves out_poses empty.
[[nodiscard]] auto expand_repeat(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses) -> std::string;

} // namespace editor
