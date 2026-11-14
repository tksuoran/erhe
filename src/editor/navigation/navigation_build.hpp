#pragma once

#include "navigation/navigation_build_settings.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct rcConfig; // Recast declares it a struct

namespace tf { class Executor; }

namespace editor {

class Navigation_mesh;

class Navigation_input_mesh
{
public:
    std::span<const glm::vec3> positions;
    std::span<const uint32_t>  triangle_indices; // 3 per triangle
};

// Input meshes in one common space, Y up (doc/plans/navigation_mesh.md N1).
class Navigation_input
{
public:
    std::vector<Navigation_input_mesh> meshes;
};

// Called from executor workers, serialized by the build.
using Navigation_build_progress = std::function<void(std::size_t tiles_done, std::size_t tiles_total)>;

// Meters to Recast cell units; bmin / bmax / width / height stay for the per-tile setup.
[[nodiscard]] auto make_recast_config(const Navigation_build_settings& settings) -> rcConfig;

// Tiled build, one executor task per tile; null with out_error set (and logged) on failure (N5).
[[nodiscard]] auto build_navigation_mesh(
    const Navigation_input&          input,
    const Navigation_build_settings& settings,
    tf::Executor&                    executor,
    const Navigation_build_progress& progress,
    std::string&                     out_error
) -> std::unique_ptr<Navigation_mesh>;

} // namespace editor
