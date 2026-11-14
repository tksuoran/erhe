#pragma once

#include "navigation/navigation_build_settings.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

class dtNavMesh;
struct dtMeshTile; // Detour declares it a struct

namespace editor {

class Navigation_polygon
{
public:
    uint32_t tile                 {0}; // index in the order the tiles were added
    uint32_t first_vertex         {0}; // into get_polygon_vertex_indices()
    uint32_t vertex_count         {0};
    uint32_t first_neighbour      {0}; // into get_neighbours()
    uint32_t neighbour_count      {0};
    uint32_t first_detail_triangle{0}; // into get_detail_triangle_vertices(), 3 vertices each
    uint32_t detail_triangle_count{0};
    float    area                 {0.0f}; // square meters
    bool     reachable            {false};
};

class Navigation_seed_result
{
public:
    glm::vec3   seed   {0.0f};
    bool        found  {false};
    uint32_t    polygon{0};
    glm::vec3   point  {0.0f}; // seed snapped onto the polygon
    std::string reason;        // set when not found
};

class Navigation_totals
{
public:
    std::size_t polygon_count          {0};
    std::size_t reachable_polygon_count{0};
    double      total_area             {0.0};
    double      reachable_area         {0.0};
};

class Detour_nav_mesh_deleter
{
public:
    void operator()(dtNavMesh* nav_mesh) const;
};

// The tiles in grid order (y, then x, then layer): the order the build adds them in.
[[nodiscard]] auto get_tiles_in_grid_order(const dtNavMesh& nav_mesh) -> std::vector<const dtMeshTile*>;

// Detour navigation mesh plus the polygon arrays filled from it (doc/plans/navigation_mesh.md N1).
class Navigation_mesh
{
public:
    Navigation_mesh(
        std::unique_ptr<dtNavMesh, Detour_nav_mesh_deleter> nav_mesh,
        const Navigation_build_settings&                    settings,
        const glm::vec3&                                    bounds_min,
        const glm::vec3&                                    bounds_max
    );
    ~Navigation_mesh() noexcept;
    Navigation_mesh(const Navigation_mesh&) = delete;
    auto operator=(const Navigation_mesh&) -> Navigation_mesh& = delete;

    // Fills the polygon arrays from the Detour tiles in tile grid order; clears the reachable flags.
    void fill_from_detour();

    // Floods from the polygons nearest to the seeds; false when every seed failed.
    [[nodiscard]] auto mark_reachable(
        std::span<const glm::vec3>           seeds,
        const glm::vec3&                     search_half_extents,
        std::vector<Navigation_seed_result>& out_results
    ) -> bool;
    // mark_reachable() without writing the polygons: one flag per polygon in out_reachable.
    [[nodiscard]] auto find_reachable(
        std::span<const glm::vec3>           seeds,
        const glm::vec3&                     search_half_extents,
        std::vector<Navigation_seed_result>& out_results,
        std::vector<uint8_t>&                out_reachable
    ) const -> bool;
    void set_reachable(std::span<const uint8_t> reachable);

    void clear_reachable();

    [[nodiscard]] auto get_detour_nav_mesh         () const -> const dtNavMesh*;
    [[nodiscard]] auto get_settings                () const -> const Navigation_build_settings&;
    [[nodiscard]] auto get_bounds_min              () const -> const glm::vec3&;
    [[nodiscard]] auto get_bounds_max              () const -> const glm::vec3&;
    [[nodiscard]] auto get_tile_count              () const -> std::size_t;
    [[nodiscard]] auto get_vertices                () const -> std::span<const glm::vec3>;
    [[nodiscard]] auto get_polygons                () const -> std::span<const Navigation_polygon>;
    [[nodiscard]] auto get_polygons                () -> std::span<Navigation_polygon>;
    [[nodiscard]] auto get_polygon_vertex_indices  () const -> std::span<const uint32_t>;
    [[nodiscard]] auto get_neighbours              () const -> std::span<const uint32_t>;
    [[nodiscard]] auto get_detail_triangle_vertices() const -> std::span<const glm::vec3>;
    [[nodiscard]] auto get_totals                  () const -> Navigation_totals;
    [[nodiscard]] auto get_polygon_centroid        (uint32_t polygon) const -> glm::vec3;

private:
    std::unique_ptr<dtNavMesh, Detour_nav_mesh_deleter> m_nav_mesh;
    Navigation_build_settings                           m_settings;
    glm::vec3                                           m_bounds_min{0.0f};
    glm::vec3                                           m_bounds_max{0.0f};
    std::size_t                                         m_tile_count{0};
    std::vector<uint32_t>                               m_slot_polygon_base; // Detour tile slot -> first polygon
    std::vector<glm::vec3>                              m_vertices;
    std::vector<Navigation_polygon>                     m_polygons;
    std::vector<uint32_t>                               m_polygon_vertex_indices;
    std::vector<uint32_t>                               m_neighbours;
    std::vector<glm::vec3>                              m_detail_triangle_vertices;
};

} // namespace editor
