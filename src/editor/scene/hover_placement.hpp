#pragma once

#include "grid/grid_frame.hpp"

#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace erhe::scene { class Mesh; class Xformable; using Node = Xformable; }

enum class Item_placement_mode : unsigned int;

namespace editor {

// The hovered point a viewport's context menu was opened at (doc/editor/windows.md "Viewport context menu").
class Hover_placement
{
public:
    glm::vec3                position{0.0f};
    std::optional<glm::vec3> normal; // unit normal of the hovered surface, in world
};

class Grid;

enum class Hover_placement_source : unsigned int {
    surface = 0, // a face of a content mesh
    grid,        // a grid plane
    analytic     // a content hover without a mesh (an analytic hover provider's item)
};

// FD2.4: the surface point a placing tool places at (Level_kits::get_placement_hover()).
class Hover_placement_hit
{
public:
    glm::vec3                          position{0.0f};
    std::optional<glm::vec3>           normal;     // unit, toward the pointer ray's origin, in world
    Hover_placement_source             source{Hover_placement_source::surface};
    std::shared_ptr<erhe::scene::Mesh> mesh;       // surface: the mesh owning the face
    std::size_t                        primitive_index{static_cast<std::size_t>(-1)}; // surface: the mesh primitive hit
    GEO::index_t                       facet{GEO::NO_INDEX};                          // surface: its facet, when known
    std::shared_ptr<const Grid>        grid;       // grid: the grid and the plane it was hit in
    Grid_frame                         grid_frame;
};

// Moves the nodes together, each with its subtree, so that the first one's origin is at the point; with
// Item_placement_mode::avoid_intersection and a normal, then along the normal until no mesh bounding box corner of
// theirs is behind the surface plane. The nodes are not yet inserted: their inserts keep the world transform.
void place_nodes_at(std::span<erhe::scene::Node* const> nodes, const Hover_placement& placement, Item_placement_mode mode);

}
