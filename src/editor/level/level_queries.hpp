#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <span>

namespace erhe::raytrace { class IScene; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class Scene_root;

enum class Surface_snap : unsigned int {
    off = 0,
    on      // the base rests on the highest surface under it (R5.11)
};

enum class Surface_orient : unsigned int {
    off = 0,
    on      // the up axis takes the surface normal, the heading is kept (R5.11)
};

class Surface_options
{
public:
    Surface_snap   snap     {Surface_snap::off};
    Surface_orient orient   {Surface_orient::off};
    glm::vec3      direction{0.0f, -1.0f, 0.0f}; // unit length
};

// A transform in the scene root's space.
class Surface_pose
{
public:
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale   {1.0f};
};

class Surface_placement
{
public:
    Surface_pose pose;          // the resting pose; the start pose when no surface was found
    float        distance{0.0f}; // how far the base moved along the direction; negative when it rose
    bool         surface_found{false};
};

[[nodiscard]] auto any_surface_option(const Surface_options& options) -> bool;

// Bounds of the mesh's primitives in the mesh's own space.
[[nodiscard]] auto mesh_local_bounds(const erhe::scene::Mesh& mesh) -> erhe::math::Aabb;

// D7.3: casts rays through the scene's raytrace scene (committed here) from the top plane of
// `local_bounds` placed at `start`; hits on `excluded` meshes are passed through.
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

} // namespace editor
