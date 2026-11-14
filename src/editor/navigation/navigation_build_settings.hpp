#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace editor {

// Recast's three region builders; make_navigation_build_settings() maps the codegen ::Navigation_partitioning here.
enum class Navigation_partitioning : uint32_t {
    watershed = 0,
    monotone  = 1,
    layers    = 2
};

// Build values in meters and degrees unless named otherwise (doc/plans/navigation_mesh.md N2).
class Navigation_build_settings
{
public:
    float                   cell_size              {0.15f};
    float                   cell_height            {0.10f};
    float                   agent_height           {1.8f};
    float                   agent_radius           {0.3f};
    float                   agent_max_climb        {0.4f};
    float                   agent_max_slope        {45.0f};
    float                   region_min_size        {1.2f};
    float                   region_merge_size      {3.0f};
    Navigation_partitioning partitioning           {Navigation_partitioning::watershed};
    float                   edge_max_length        {12.0f};
    float                   edge_max_error         {1.3f}; // cells
    int                     verts_per_poly         {6};
    float                   detail_sample_distance {0.9f};
    float                   detail_sample_max_error{0.1f};
    float                   tile_size              {19.2f};
    glm::vec3               region_min             {0.0f}; // both zero: the union of the input bounds
    glm::vec3               region_max             {0.0f};
    float                   seed_search_radius     {1.0f};
    float                   seed_search_height     {3.0f};

    [[nodiscard]] auto get_seed_search_half_extents() const -> glm::vec3
    {
        return glm::vec3{seed_search_radius, seed_search_height, seed_search_radius};
    }
};

} // namespace editor
