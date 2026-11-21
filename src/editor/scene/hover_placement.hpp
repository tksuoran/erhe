#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <span>

namespace erhe::scene { class Xformable; using Node = Xformable; }

enum class Item_placement_mode : unsigned int;

namespace editor {

// The hovered point a viewport's context menu was opened at (doc/editor/windows.md "Viewport context menu").
class Hover_placement
{
public:
    glm::vec3                position{0.0f};
    std::optional<glm::vec3> normal; // unit normal of the hovered surface, in world
};

// Moves the nodes together, each with its subtree, so that the first one's origin is at the point; with
// Item_placement_mode::avoid_intersection and a normal, then along the normal until no mesh bounding box corner of
// theirs is behind the surface plane. The nodes are not yet inserted: their inserts keep the world transform.
void place_nodes_at(std::span<erhe::scene::Node* const> nodes, const Hover_placement& placement, Item_placement_mode mode);

}
