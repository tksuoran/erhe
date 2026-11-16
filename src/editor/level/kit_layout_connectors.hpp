#pragma once

#include "erhe_math/curve_path.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Scene-free part of a curve layout's connector switches (U39, doc/editor/level_tools.md "Layout connector switches").

namespace editor {

// The six directions a layout child's connector faces, in the child's element frame (tangent x, binormal y, normal z).
enum class Kit_layout_direction : unsigned int {
    plus_tangent = 0,
    minus_tangent,
    plus_normal,
    minus_normal,
    plus_binormal,
    minus_binormal
};

inline constexpr std::size_t c_layout_direction_count = 6;

// "+tangent", "-tangent", "+normal", "-normal", "+binormal", "-binormal".
[[nodiscard]] auto c_str(Kit_layout_direction direction) -> const char*;

// Whether a switch hides its connectors.
enum class Kit_layout_connector_visibility : unsigned int {
    shown = 0,
    hidden
};

// The seven connector switches of a path or segments layout node (U39); all shown by default.
class Kit_layout_connector_hiding
{
public:
    Kit_layout_connector_visibility                                       all{Kit_layout_connector_visibility::shown};
    std::array<Kit_layout_connector_visibility, c_layout_direction_count> directions{}; // by Kit_layout_direction

    [[nodiscard]] auto is_hidden(Kit_layout_direction direction) const -> bool;
    [[nodiscard]] auto hides_any() const -> bool;
    void               set      (Kit_layout_direction direction, Kit_layout_connector_visibility visibility);

    [[nodiscard]] auto operator==(const Kit_layout_connector_hiding& other) const -> bool = default;
};

// Where a child stands in its layout's run: an open curve's first (head) and last (tail) child face one neighbour.
enum class Kit_layout_child_role : unsigned int {
    inner = 0, // every child of a closed curve, and an open curve's children between its first and last
    head,
    tail,
    only       // the one child of a layout: no neighbour
};

[[nodiscard]] auto c_str(Kit_layout_child_role role) -> const char*;

[[nodiscard]] auto get_layout_child_role(std::size_t index, std::size_t count, erhe::math::Curve_closure closure) -> Kit_layout_child_role;

// Why a layout child's connector is hidden, or not.
enum class Kit_layout_hide_reason : unsigned int {
    shown = 0,
    hide_all,   // the all switch
    direction   // the switch of the direction it faces
};

[[nodiscard]] auto c_str(Kit_layout_hide_reason reason) -> const char*;

// The axis of the largest absolute component with its sign; ties go to tangent, then normal, then binormal.
[[nodiscard]] auto classify_layout_direction(const glm::vec3& outward_in_element) -> Kit_layout_direction;

// A node-local outward axis in the element frame of a child of the local scale (the rotation is the frame, so only the
// scale acts: a face normal divides by it, a mirror flips); unit, nullopt for a zero or non-finite scale axis.
[[nodiscard]] auto get_outward_in_element(const glm::vec3& local_outward, const glm::vec3& scale) -> std::optional<glm::vec3>;

// U39: an inner child follows every switch; a head follows them for its +tangent connectors only, a tail for its
// -tangent ones only; an only child shows everything.
[[nodiscard]] auto get_layout_hide_reason(
    const Kit_layout_connector_hiding& hiding,
    Kit_layout_child_role              role,
    Kit_layout_direction               direction
) -> Kit_layout_hide_reason;

// A child of a locked path or segments layout as its connector switches read it.
class Kit_layout_child
{
public:
    std::uint64_t               layout_id{0};
    Kit_layout_connector_hiding hiding;
    Kit_layout_child_role       role            {Kit_layout_child_role::inner};
    glm::vec3                   scale{1.0f}; // the child's local scale; its local rotation is the element frame
};

class Kit_layout_connector_verdict
{
public:
    std::optional<Kit_layout_direction> direction; // none when the child's frame degenerates
    Kit_layout_hide_reason              reason{Kit_layout_hide_reason::shown};
};

// The direction a node-local connector outward faces in the child's element frame and whether the switches hide it.
[[nodiscard]] auto get_layout_connector_verdict(const Kit_layout_child& child, const glm::vec3& local_outward) -> Kit_layout_connector_verdict;

} // namespace editor
