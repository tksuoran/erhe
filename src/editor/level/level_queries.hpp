#pragma once

#include "level/kit_shape.hpp"
#include "level/level_region.hpp"
#include "level/surface_base_frame.hpp"
#include "level/surface_options.hpp"

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace erhe::physics  { class ICollision_shape; }
namespace erhe::raytrace { class IScene; }
namespace erhe::scene    { class Mesh; class Mesh_primitive; }

namespace editor {

class Kit_shape_library;
class Scene_root;

class Surface_placement
{
public:
    Surface_pose pose;          // the resting pose; the start pose itself when no place was found or the start already rests
    float        distance{0.0f}; // how far the base moved along the direction; negative when it rose
    bool         surface_found{false}; // false when no resting place lies within the maximum distance
};

// Bounds of the mesh's primitives in the mesh's own space.
[[nodiscard]] auto mesh_local_bounds(const erhe::scene::Mesh& mesh) -> erhe::math::Aabb;
// The same of primitives a mesh is about to take.
[[nodiscard]] auto mesh_local_bounds(std::span<const erhe::scene::Mesh_primitive> primitives) -> erhe::math::Aabb;

// D7.3: finds the nearest resting place along the direction for `local_bounds` placed at `start` through the scene's
// raytrace scene (committed here); `excluded` meshes are not there. Every mesh rests on its local -Y face (U51); the
// options' support rule chooses the base lines that support and block without orient (U52).
[[nodiscard]] auto find_surface_placement(
    Scene_root&                              scene_root,
    const erhe::math::Aabb&                  local_bounds,
    const Surface_pose&                      start,
    const Surface_options&                   options,
    std::span<const erhe::scene::Mesh* const> excluded
) -> Surface_placement;

// The same against a raytrace scene the caller committed after its last change, so a caller
// placing several meshes commits once.
[[nodiscard]] auto find_surface_placement(
    erhe::raytrace::IScene&                  committed_rt_scene,
    const erhe::math::Aabb&                  local_bounds,
    const Surface_pose&                      start,
    const Surface_options&                   options,
    std::span<const erhe::scene::Mesh* const> excluded
) -> Surface_placement;

class Point_surface
{
public:
    bool      found   {false};
    glm::vec3 position{0.0f};             // the point moved along the direction onto the surface
    glm::vec3 normal  {0.0f, 1.0f, 0.0f}; // of the surface there, by the tie order of D7.3
    float     distance{0.0f};             // how far the point moved along the direction; negative when against it
};

// U51 (Transform tool drags): the nearest surface on one line through the point, up to forward_distance along the options'
// unit direction and backward_distance against it; a point has no extent, so nothing is tested for fit.
[[nodiscard]] auto find_point_surface(
    erhe::raytrace::IScene&                   committed_rt_scene,
    const glm::vec3&                          point,
    const Surface_options&                    options,
    std::span<const erhe::scene::Mesh* const> excluded
) -> Point_surface;

// The content meshes of the scene that carry a kit piece record, by id.
void get_kit_piece_meshes(Scene_root& scene_root, std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes);

// D7.1: the content meshes whose world bounds intersect the region, by id; cleared and filled.
void find_region_contents(Scene_root& scene_root, const Level_region& region, std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes);

class Height_map
{
public:
    Height_map_grid              grid;
    std::vector<Height_map_cell> cells; // rows * columns, row after row
    bool                         navigation_available{false};
    bool                         navigation_stale    {false};
};

// D7.2: one downward ray per cell center from grid.max.y to grid.min.y (raytrace scene committed
// here); faces seen from behind are passed through. Cells are cleared and refilled.
void make_height_map(Scene_root& scene_root, const Height_map_grid& grid, Height_map& out_height_map);

inline constexpr float c_default_support_tolerance = 0.01f;

class Support_result
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    bool                               surface_found{false};
    float                              distance     {0.0f}; // smallest snap distance from the base; negative when sunk
    bool                               supported    {false};
};

// D7.4: the snap rays of D7.3 (down) under each mesh, only the mesh itself passed through; a mesh
// is supported when a surface lies within tolerance under its base. Cleared and filled.
void find_support(
    Scene_root&                                         scene_root,
    std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    float                                               tolerance,
    std::vector<Support_result>&                        out_results
);

inline constexpr float c_default_overlap_tolerance = 0.01f;
inline constexpr float c_default_min_island_area   = 1.0f; // square meters

enum class Overlap_measure : unsigned int {
    exact = 0,    // the meshes' own collision shapes
    convex_hull,  // a convex hull of a mesh stood in for its shape (no shape, or two triangle meshes)
    not_measured  // the physics backend collides neither form; depth is the bounds overlap
};

[[nodiscard]] auto c_str(Overlap_measure measure) -> const char*;

class Overlap_result
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh_a; // the smaller id
    std::shared_ptr<erhe::scene::Mesh> mesh_b;
    float                              depth  {0.0f};
    Overlap_measure                    measure{Overlap_measure::exact};
    glm::vec3                          position{0.0f}; // center of the box both world bounds cover
};

class Overlap_report
{
public:
    std::vector<Overlap_result> overlaps;
    std::size_t                 tested_count     {0};
    std::size_t                 bounds_pair_count{0}; // pairs the bounds pass kept for the exact test

    // Scratch, capacity kept between calls.
    std::vector<std::shared_ptr<erhe::scene::Mesh>>             tested;
    std::vector<erhe::math::Aabb>                               bounds;
    std::vector<std::size_t>                                    order;
    std::vector<Bounds_pair>                                    pairs;
    std::vector<std::shared_ptr<erhe::physics::ICollision_shape>> shapes;
    std::vector<std::shared_ptr<erhe::physics::ICollision_shape>> hulls;
};

// D7.5: the meshes without an excluded tag, pairs of world bounds overlapping by more than tolerance,
// then the penetration depth of their collision shapes; pairs deeper than tolerance are kept.
void find_overlaps(
    std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    float                                               tolerance,
    std::span<const std::string>                        exclude_tags,
    Overlap_report&                                     out_report
);

class Level_finding
{
public:
    Level_finding_kind                                kind{Level_finding_kind::unsupported};
    glm::vec3                                         position{0.0f};
    std::array<std::shared_ptr<erhe::scene::Mesh>, 2> meshes;
    std::size_t                                       mesh_count{0};
    float                                             value     {0.0f}; // distance, depth or area
    bool                                              has_value {false};
    Overlap_measure                                   measure   {Overlap_measure::exact}; // overlap findings
    uint32_t                                          island    {0};                      // island findings
    std::string                                       detail;
};

class Level_check_options
{
public:
    float                        support_tolerance{c_default_support_tolerance};
    float                        overlap_tolerance{c_default_overlap_tolerance};
    float                        min_island_area  {c_default_min_island_area};
    std::span<const std::string> exclude_tags; // left out of the support and overlap parts
};

class Level_check
{
public:
    std::vector<Level_finding> findings;
    std::size_t                piece_count         {0};
    std::size_t                excluded_count      {0};
    Kit_agent                  agent;
    bool                       navigation_available{false};
    bool                       navigation_stale    {false};
    std::size_t                island_count        {0};

    // Scratch, capacity kept between calls.
    std::vector<std::shared_ptr<erhe::scene::Mesh>> pieces;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> tested;
    std::vector<Support_result>                     support;
    Overlap_report                                  overlap;
};

// D7.6 over every kit piece: support, overlap, unreachable islands above min_island_area and R2.10
// over the pieces' parameter values, in that order.
void run_level_check(
    Scene_root&                scene_root,
    const Kit_shape_library&   library,
    const Level_check_options& options,
    Level_check&               out_check
);

} // namespace editor
