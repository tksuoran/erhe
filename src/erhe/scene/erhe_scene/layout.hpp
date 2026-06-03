#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::scene {

class Node;

// The kind of arrangement a Layout performs on its node's children.
enum class Layout_type : unsigned int {
    stack = 0, // children distributed along a single (primary) signed axis
    grid,      // children placed into an explicit X/Y/Z cell grid
    flow       // children wrapped into lines -> sheets -> tertiary stack
};

// Signed principal axis selection (axis + sign).
enum class Axis_direction : unsigned int {
    pos_x = 0,
    neg_x,
    pos_y,
    neg_y,
    pos_z,
    neg_z
};

[[nodiscard]] auto axis_index (Axis_direction direction) -> int;       // 0, 1, 2
[[nodiscard]] auto axis_sign  (Axis_direction direction) -> float;     // +1.0f / -1.0f
[[nodiscard]] auto axis_vector(Axis_direction direction) -> glm::vec3; // signed unit vector

// A Layout is a Node_attachment that owns a volume (an axis-aligned box in the
// node's local space) and computes the local transform of each direct child
// node so the children are arranged inside that volume.
class Layout : public erhe::Item<Item_base, Node_attachment, Layout, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    using Type = Layout_type;

    static constexpr const char* c_type_strings[] = {
        "Stack",
        "Grid",
        "Flow"
    };
    static constexpr const char* c_axis_direction_strings[] = {
        "+X", "-X", "+Y", "-Y", "+Z", "-Z"
    };

    Layout(const Layout&);
    Layout& operator=(const Layout&) = delete;
    ~Layout() noexcept override;

    explicit Layout(std::string_view name);
    Layout(const Layout& src, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Layout"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::node_attachment | erhe::Item_type::layout;
    }

    // Implements Node_attachment
    auto clone_attachment() const -> std::shared_ptr<Node_attachment> override;

    // Public API
    // Recompute and apply the local transform of every direct child. The editor
    // calls this once per frame for every layout node (App_scenes::update_layout_nodes)
    // before the world-transform passes. A dirty/serial-gated optimization is
    // deliberately left for later; recomputing each frame is simple and correct.
    void update();

    // Parameters (kept serialization-friendly: POD / glm / enum / vector)
    Type             type     {Type::stack};
    erhe::math::Aabb volume   {glm::vec3{-0.5f, -0.5f, -0.5f}, glm::vec3{0.5f, 0.5f, 0.5f}};
    Axis_direction   primary  {Axis_direction::pos_x};
    Axis_direction   secondary{Axis_direction::pos_y};
    Axis_direction   tertiary {Axis_direction::pos_z};
    glm::vec3        gap      {0.0f, 0.0f, 0.0f}; // spacing per level (primary, secondary, tertiary)
    glm::ivec3       grid_track_count{1, 1, 1};
    std::array<std::vector<float>, 3> grid_track_extent{}; // grid: per-track extents; empty => uniform

private:
    void layout_stack(Node& layout_node);
    // layout_grid / layout_flow are added in later steps.
};

// Content bounding box of a node expressed in that node's own local space:
// the node's own mesh primitives plus its descendants. A descendant that is
// itself a layout node contributes its declared volume (not its geometry),
// which both matches intent and breaks the recursion cycle.
[[nodiscard]] auto compute_content_local_aabb(const Node& node) -> erhe::math::Aabb;

} // namespace erhe::scene
