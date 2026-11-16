#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Kit_parameter_values;

// A named frame on a shape where another shape attaches (R5.3, D1.12): unit outward and up axes, up normal to outward.
// The name is a literal (static storage), so filling and copying connectors allocates nothing.
class Kit_connector
{
public:
    const char* name{""};
    glm::vec3   position{0.0f};
    glm::vec3   outward {0.0f, 0.0f, 1.0f};
    glm::vec3   up      {0.0f, 1.0f, 0.0f};
};

// A connector switch's state (U33).
enum class Kit_connector_state : unsigned int
{
    enabled = 0,
    disabled
};

// The connector switches of a placed piece (U33, doc/editor/level_tools.md "Connector switches"): a connector is
// enabled when the piece switch is on and its name is not in the disabled list.
class Kit_connector_switches
{
public:
    bool        connectors_enabled{true};
    std::string disabled_connectors; // names separated by commas; canonical: sorted, unique, no spaces

    [[nodiscard]] auto is_enabled (std::string_view connector_name) const -> bool;
    [[nodiscard]] auto is_disabled(std::string_view connector_name) const -> bool; // listed, whatever the piece switch
};

// The names of a disabled list text, read leniently (spaces trimmed, empty entries skipped); allocates.
[[nodiscard]] auto parse_connector_names(std::string_view text) -> std::vector<std::string>;

// The canonical text of a disabled list (sorted, unique, comma separated).
[[nodiscard]] auto make_connector_names_text(std::vector<std::string> names) -> std::string;

// U33: why a disabled list text cannot be written for a shape of the connectors - the names it has not (exact, case
// sensitive) or, with every name known, the text not canonical; empty when the text is accepted.
[[nodiscard]] auto check_disabled_connectors_text(std::string_view text, std::span<const Kit_connector> connectors) -> std::string;

// The disabled list text with the name's switch set to the state, canonical.
[[nodiscard]] auto set_connector_state(std::string_view disabled_connectors, std::string_view connector_name, Kit_connector_state state) -> std::string;

// Removes the connectors that the switches disable, keeping the others in their order.
void remove_disabled_connectors(const Kit_connector_switches& switches, std::vector<Kit_connector>& connectors);

// The pose (rotation, then translation) that joins a piece's connector to a target connector (D3.12).
class Kit_join_pose
{
public:
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

// A connector position of the connector snap search (D3.13); piece_id and connector_index order ties.
class Kit_connector_point
{
public:
    glm::vec3     position       {0.0f};
    std::uint64_t piece_id       {0};
    std::size_t   connector_index{0}; // in the shape's declared order
};

class Kit_connector_snap_match
{
public:
    bool        found   {false};
    std::size_t moved   {0}; // index in the moved points
    std::size_t other   {0}; // index in the other points
    float       distance{0.0f};
};

// D3.13: the closest (moved, other) pair at most max_distance apart (inclusive); none when max_distance is not positive.
[[nodiscard]] auto find_connector_snap(
    std::span<const Kit_connector_point> moved,
    std::span<const Kit_connector_point> others,
    float                                max_distance
) -> Kit_connector_snap_match;

// U27: the handle a translate drag moves along; free (no handle, or Connector_snap_axis_mode::full_offset) keeps the full offset.
enum class Kit_connector_snap_constraint_kind : unsigned int {
    free = 0,
    axis,  // direction: the dragged axis
    plane  // direction: the dragged plane's normal
};

class Kit_connector_snap_constraint
{
public:
    Kit_connector_snap_constraint_kind kind     {Kit_connector_snap_constraint_kind::free};
    glm::vec3                          direction{0.0f}; // world, not necessarily unit
};

// U27: the snap offset's component along the axis or in the plane; the offset itself when free or the direction is degenerate.
[[nodiscard]] auto constrain_connector_snap_offset(const glm::vec3& offset, const Kit_connector_snap_constraint& constraint) -> glm::vec3;

// The linear part's columns are mutually orthogonal (within 1e-4 of their lengths): a translation, rotation and scale
// (a Trs_transform) holds the matrix without shear.
[[nodiscard]] auto has_orthogonal_axes(const glm::mat3& m) -> bool;

// Unit, mutually orthogonal columns (within 1e-5) and determinant +1: a proper rotation.
[[nodiscard]] auto is_rotation_matrix(const glm::mat3& m) -> bool;

// D3.12: the rotation R with linear = R * diag(scale) when one exists (a mirrored scale included, which a decomposition
// would re-sign); nullopt otherwise.
[[nodiscard]] auto rotation_for_scale(const glm::mat3& linear, const glm::vec3& scale) -> std::optional<glm::quat>;

// The connectors of a shape at the values, in the shape's declared order; none for a shape without connectors.
// No allocation once the caller's vector has the capacity (a drag step may call it).
void get_shape_connectors(const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors);

[[nodiscard]] auto find_connector(std::span<const Kit_connector> connectors, std::string_view name) -> const Kit_connector*;

// "a, b, c"; "(none)" when empty.
[[nodiscard]] auto connector_names_text(std::span<const Kit_connector> connectors) -> std::string;

// The connectors carried by a rigid pose into the caller's vector (cleared, capacity kept).
void transform_connectors(std::span<const Kit_connector> connectors, const glm::vec3& position, const glm::quat& rotation, std::vector<Kit_connector>& out_connectors);

// The connector carried by a rigid pose (a node's world translation and rotation).
[[nodiscard]] auto transform_connector(const Kit_connector& connector, const glm::vec3& position, const glm::quat& rotation) -> Kit_connector;

// D1.12: the connector carried by any affine matrix (scale, shear, mirror): the position by the matrix, outward as a face
// normal (inverse transpose), up as an in-face direction, both normalized; nullopt for a singular matrix.
[[nodiscard]] auto transform_connector(const Kit_connector& connector, const glm::mat4& world_from_node) -> std::optional<Kit_connector>;

// As above for every connector into the caller's vector (cleared, capacity kept); false and empty when one degenerates.
[[nodiscard]] auto transform_connectors(std::span<const Kit_connector> connectors, const glm::mat4& world_from_node, std::vector<Kit_connector>& out_connectors) -> bool;

// The linear part collapses an axis (singular within 1e-6 of the product of its column lengths) or is not finite.
[[nodiscard]] auto is_degenerate_frame(const glm::mat3& linear) -> bool;

// D3.13: a moved connector in the frame of the moved root it rides with, from root_from_node at the drag begin: outward
// as a normal, neither axis normalized, so transform_connector() by a step's world_from_root gives the node's frame.
[[nodiscard]] auto connector_in_root(const Kit_connector& connector, const glm::mat4& root_from_node) -> Kit_connector;

// D3.13: the world position of a connector_in_root() position at a step's world_from_root (any rotation, scale or
// mirror the step applied); nullopt when that frame degenerates, so a collapsed root does not snap.
[[nodiscard]] auto carry_connector_position(const glm::vec3& position_in_root, const glm::mat4& world_from_root) -> std::optional<glm::vec3>;

// D3.12: the piece pose putting its local connector on the target's: positions coincide (moved gap along the target's
// outward), outward axes oppose, up axes agree after twist_steps quarter turns about the target's outward axis.
[[nodiscard]] auto join_connectors(const Kit_connector& piece_connector, const Kit_connector& target_connector, int twist_steps, float gap) -> Kit_join_pose;

// D3.12: the rigid world motion that carries the node's connector, transformed by world_from_node, onto the target's
// world connector (join_connectors() of the transformed frame); nullopt when the transformed frame degenerates.
[[nodiscard]] auto join_motion(
    const Kit_connector& local_connector,
    const glm::mat4&     world_from_node,
    const Kit_connector& target_connector,
    int                  twist_steps,
    float                gap
) -> std::optional<Kit_join_pose>;

// The motion's matrix (translation times rotation) times m.
[[nodiscard]] auto apply_motion(const Kit_join_pose& motion, const glm::mat4& m) -> glm::mat4;

// A motion of at most 1e-4 m and 1e-4 rad: the piece is joined already.
[[nodiscard]] auto is_identity_motion(const Kit_join_pose& motion) -> bool;

// What a connector indicator shows (doc/editor/level_tools.md "Connector indicators").
enum class Kit_connector_indicator_role : unsigned int
{
    piece = 0, // a connector of a shown piece
    chosen,    // chosen in the Level Kit window's Connect rows
    moved,     // of the moved set during a snapping drag
    target,    // a target within reach of a moved connector during a snapping drag
    snapped    // the pair the drag's last step snapped by
};

class Kit_connector_indicator
{
public:
    Kit_connector                frame;    // in world space
    std::uint64_t                piece_id{0};
    Kit_connector_indicator_role role    {Kit_connector_indicator_role::piece};
};

[[nodiscard]] auto c_str(Kit_connector_indicator_role role) -> const char*;

} // namespace editor
