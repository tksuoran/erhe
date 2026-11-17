#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Kit_parameter_values;

// The light type a frame's new light node starts as; erhe::scene::Light_type point and spot.
enum class Kit_light_type : unsigned int {
    point = 0,
    spot
};

[[nodiscard]] auto c_str(Kit_light_type type) -> const char*;

// The values a new light node of a frame starts with; afterwards they are the node's own (P2, U36).
// Shadow casting always starts off.
class Kit_light_start
{
public:
    Kit_light_type type            {Kit_light_type::point};
    glm::vec3      color           {1.0f, 0.78f, 0.52f}; // warm white, linear
    float          intensity       {40.0f};              // candela
    float          range           {8.0f};               // meters
    float          inner_spot_angle{0.0f};               // radians, full cone; spot only
    float          outer_spot_angle{0.0f};
};

// A frame of a shape where a light node sits (D3.15): a name, a position and the unit direction the light shines in,
// in the piece's local space. The name is a literal (static storage), as a connector's.
class Kit_light_frame
{
public:
    const char*     name     {""};
    glm::vec3       position {0.0f};
    glm::vec3       direction{0.0f, -1.0f, 0.0f};
    Kit_light_start start    {};
};

// A shape states at most this many light frames at any value; a unit test checks every shape.
inline constexpr std::size_t c_max_light_frames = 4;

// The light frames of a shape at the values; none for a shape without light frames.
void get_shape_light_frames(const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames);

// Why the frames are not a valid statement: more than c_max_light_frames, an empty or repeated name, a direction that is
// not unit length, a value that is not finite, a non-positive range or intensity, spot angles out of order; empty when valid.
[[nodiscard]] auto check_light_frames(std::span<const Kit_light_frame> frames) -> std::string;

[[nodiscard]] auto find_light_frame(std::span<const Kit_light_frame> frames, std::string_view name) -> const Kit_light_frame*;

// The light node's local rotation at a frame: node -Z (where an erhe light shines) on the direction, node +Y as close to
// the piece's +Y as the direction allows (-Z for a vertical direction).
[[nodiscard]] auto get_light_frame_rotation(const glm::vec3& direction) -> glm::quat;

// The node's local transform is the frame's: the translation within 1e-5 m, the rotation within 1e-5 and scale 1.
[[nodiscard]] auto is_at_light_frame(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale, const Kit_light_frame& frame) -> bool;

inline constexpr std::size_t c_no_light_node = std::numeric_limits<std::size_t>::max();

// How a piece's light nodes meet its frames (P2 "Keeping edits"): by the recorded frame name, the first node of a name
// taking the frame.
class Kit_light_match
{
public:
    std::vector<std::size_t> frame_nodes;    // per frame: the index of its node, c_no_light_node for a frame needing a new one
    std::vector<std::size_t> removed_nodes;  // the nodes recording a frame the values do not state, in node order
    std::vector<std::size_t> released_nodes; // the nodes recording a frame an earlier node took (a user's copy), in node order
};

// node_frames: the frame name each of the piece's light nodes records, in child order.
void match_light_frames(std::span<const Kit_light_frame> frames, std::span<const std::string> node_frames, Kit_light_match& out_match);

} // namespace editor
