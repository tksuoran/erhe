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

class Navigation_query;

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
    uint32_t island               {0};    // into get_islands()
    bool     reachable            {false};
};

// A set of polygons joined by neighbour links (doc/editor/navigation.md "Islands and queries").
class Navigation_island
{
public:
    uint32_t  polygon_count{0};
    double    area         {0.0};  // square meters
    glm::vec3 bounds_min   {0.0f}; // over the detail triangles
    glm::vec3 bounds_max   {0.0f};
    glm::vec3 point        {0.0f}; // centroid of its largest polygon
    uint32_t  largest_polygon{0};
    bool      reachable    {false};
};

class Navigation_point_result
{
public:
    bool      found    {false};
    glm::vec3 point    {0.0f}; // nearest point on the polygon
    uint32_t  polygon  {0};
    uint32_t  island   {0};
    bool      reachable{false};
};

enum class Navigation_path_failure
{
    none = 0,
    no_start_polygon,
    no_end_polygon,
    different_islands,
    search_failed
};

class Navigation_path_result
{
public:
    Navigation_point_result start;
    Navigation_point_result end;
    bool                    found   {false}; // start and end on one island and a path returned
    bool                    complete{false}; // the path reaches the end polygon
    float                   length  {0.0f};  // along the corners, meters
    Navigation_path_failure failure {Navigation_path_failure::none};
    std::vector<glm::vec3>  corners;         // cleared and filled by find_path(); start and end included
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
    std::size_t island_count           {0};
    std::size_t reachable_island_count {0};
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

    // Fills the polygon arrays and islands from the Detour tiles in tile grid order and creates the query; clears the reachable flags.
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

    // Main thread only: these use the mesh's one Detour query.
    [[nodiscard]] auto find_nearest_point(const glm::vec3& position, const glm::vec3& half_extents) -> Navigation_point_result;
    void find_path(const glm::vec3& start, const glm::vec3& end, const glm::vec3& half_extents, Navigation_path_result& out_result);

    [[nodiscard]] auto get_polygon_ref  (uint32_t polygon) const -> uint64_t; // the Detour dtPolyRef
    [[nodiscard]] auto find_polygon_index(uint64_t polygon_ref, uint32_t& out_polygon) const -> bool;

    [[nodiscard]] auto get_detour_nav_mesh         () const -> const dtNavMesh*;
    [[nodiscard]] auto get_settings                () const -> const Navigation_build_settings&;
    [[nodiscard]] auto get_bounds_min              () const -> const glm::vec3&;
    [[nodiscard]] auto get_bounds_max              () const -> const glm::vec3&;
    [[nodiscard]] auto get_tile_count              () const -> std::size_t;
    [[nodiscard]] auto get_vertices                () const -> std::span<const glm::vec3>;
    [[nodiscard]] auto get_polygons                () const -> std::span<const Navigation_polygon>;
    [[nodiscard]] auto get_polygon_vertex_indices  () const -> std::span<const uint32_t>;
    [[nodiscard]] auto get_neighbours              () const -> std::span<const uint32_t>;
    [[nodiscard]] auto get_detail_triangle_vertices() const -> std::span<const glm::vec3>;
    [[nodiscard]] auto get_islands                 () const -> std::span<const Navigation_island>;
    [[nodiscard]] auto get_reachable_island_count  () const -> std::size_t;
    [[nodiscard]] auto get_totals                  () const -> Navigation_totals;
    [[nodiscard]] auto get_polygon_centroid        (uint32_t polygon) const -> glm::vec3;

private:
    void fill_islands();
    void update_island_reachable();
    [[nodiscard]] auto make_point_result(uint64_t polygon_ref, const glm::vec3& point) const -> Navigation_point_result;

    std::unique_ptr<dtNavMesh, Detour_nav_mesh_deleter> m_nav_mesh;
    std::unique_ptr<Navigation_query>                   m_query;
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
    std::vector<uint64_t>                               m_polygon_refs;
    std::vector<Navigation_island>                      m_islands;
    std::size_t                                         m_reachable_island_count{0};
};

} // namespace editor
