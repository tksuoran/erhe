#include "navigation/navigation_mesh.hpp"
#include "editor_log.hpp"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <fmt/format.h>

#include <algorithm>
#include <limits>

namespace editor {

namespace {

constexpr uint32_t c_no_polygon_base = std::numeric_limits<uint32_t>::max();

class Detour_query_deleter
{
public:
    void operator()(dtNavMeshQuery* query) const
    {
        dtFreeNavMeshQuery(query);
    }
};

[[nodiscard]] auto to_vec3(const float* v) -> glm::vec3
{
    return glm::vec3{v[0], v[1], v[2]};
}

[[nodiscard]] auto triangle_area(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> float
{
    return 0.5f * glm::length(glm::cross(b - a, c - a));
}

} // anonymous namespace

auto get_tiles_in_grid_order(const dtNavMesh& nav_mesh) -> std::vector<const dtMeshTile*>
{
    std::vector<const dtMeshTile*> tiles;
    for (int i = 0, end = nav_mesh.getMaxTiles(); i < end; ++i) {
        const dtMeshTile* tile = nav_mesh.getTile(i);
        if ((tile != nullptr) && (tile->header != nullptr)) {
            tiles.push_back(tile);
        }
    }
    std::sort(
        tiles.begin(), tiles.end(),
        [](const dtMeshTile* lhs, const dtMeshTile* rhs) {
            if (lhs->header->y != rhs->header->y) {
                return lhs->header->y < rhs->header->y;
            }
            if (lhs->header->x != rhs->header->x) {
                return lhs->header->x < rhs->header->x;
            }
            return lhs->header->layer < rhs->header->layer;
        }
    );
    return tiles;
}

void Detour_nav_mesh_deleter::operator()(dtNavMesh* nav_mesh) const
{
    dtFreeNavMesh(nav_mesh);
}

Navigation_mesh::Navigation_mesh(
    std::unique_ptr<dtNavMesh, Detour_nav_mesh_deleter> nav_mesh,
    const Navigation_build_settings&                    settings,
    const glm::vec3&                                    bounds_min,
    const glm::vec3&                                    bounds_max
)
    : m_nav_mesh  {std::move(nav_mesh)}
    , m_settings  {settings}
    , m_bounds_min{bounds_min}
    , m_bounds_max{bounds_max}
{
}

Navigation_mesh::~Navigation_mesh() noexcept = default;

void Navigation_mesh::fill_from_detour()
{
    m_vertices.clear();
    m_polygons.clear();
    m_polygon_vertex_indices.clear();
    m_neighbours.clear();
    m_detail_triangle_vertices.clear();
    m_slot_polygon_base.clear();
    m_tile_count = 0;

    const dtNavMesh& nav_mesh  = *m_nav_mesh;
    const int        max_tiles = nav_mesh.getMaxTiles();
    m_slot_polygon_base.resize(static_cast<std::size_t>(max_tiles), c_no_polygon_base);

    const std::vector<const dtMeshTile*> tiles = get_tiles_in_grid_order(nav_mesh);
    m_tile_count = tiles.size();

    // Polygon bases first, so links into later tiles resolve.
    uint32_t polygon_base = 0;
    for (const dtMeshTile* tile : tiles) {
        const unsigned int slot = nav_mesh.decodePolyIdTile(nav_mesh.getPolyRefBase(tile));
        m_slot_polygon_base[slot] = polygon_base;
        polygon_base += static_cast<uint32_t>(tile->header->polyCount);
    }
    m_polygons.reserve(polygon_base);

    std::vector<uint32_t> polygon_neighbours;
    for (std::size_t tile_index = 0; tile_index < tiles.size(); ++tile_index) {
        const dtMeshTile*   tile        = tiles[tile_index];
        const dtMeshHeader& header      = *tile->header;
        const uint32_t      vertex_base = static_cast<uint32_t>(m_vertices.size());
        for (int v = 0; v < header.vertCount; ++v) {
            m_vertices.push_back(to_vec3(&tile->verts[v * 3]));
        }
        for (int p = 0; p < header.polyCount; ++p) {
            const dtPoly& poly = tile->polys[p];
            Navigation_polygon polygon{};
            polygon.tile = static_cast<uint32_t>(tile_index);
            if (poly.getType() != DT_POLYTYPE_GROUND) {
                // Off-mesh connections are not built (doc/plans/navigation_mesh.md "Later").
                polygon.first_vertex          = static_cast<uint32_t>(m_polygon_vertex_indices.size());
                polygon.first_neighbour       = static_cast<uint32_t>(m_neighbours.size());
                polygon.first_detail_triangle = static_cast<uint32_t>(m_detail_triangle_vertices.size() / 3);
                m_polygons.push_back(polygon);
                continue;
            }

            polygon.first_vertex = static_cast<uint32_t>(m_polygon_vertex_indices.size());
            polygon.vertex_count = poly.vertCount;
            for (unsigned int k = 0; k < poly.vertCount; ++k) {
                m_polygon_vertex_indices.push_back(vertex_base + poly.verts[k]);
            }

            polygon_neighbours.clear();
            for (unsigned int link = poly.firstLink; link != DT_NULL_LINK; link = tile->links[link].next) {
                const dtPolyRef ref = tile->links[link].ref;
                if (ref == 0) {
                    continue;
                }
                unsigned int salt          = 0;
                unsigned int neighbour_slot = 0;
                unsigned int neighbour_poly = 0;
                nav_mesh.decodePolyId(ref, salt, neighbour_slot, neighbour_poly);
                const uint32_t base = m_slot_polygon_base[neighbour_slot];
                if (base != c_no_polygon_base) {
                    polygon_neighbours.push_back(base + neighbour_poly);
                }
            }
            std::sort(polygon_neighbours.begin(), polygon_neighbours.end());
            polygon_neighbours.erase(std::unique(polygon_neighbours.begin(), polygon_neighbours.end()), polygon_neighbours.end());
            polygon.first_neighbour = static_cast<uint32_t>(m_neighbours.size());
            polygon.neighbour_count = static_cast<uint32_t>(polygon_neighbours.size());
            m_neighbours.insert(m_neighbours.end(), polygon_neighbours.begin(), polygon_neighbours.end());

            const dtPolyDetail& detail = tile->detailMeshes[p];
            polygon.first_detail_triangle = static_cast<uint32_t>(m_detail_triangle_vertices.size() / 3);
            polygon.detail_triangle_count = detail.triCount;
            float area = 0.0f;
            for (unsigned int t = 0; t < detail.triCount; ++t) {
                const unsigned char* triangle = &tile->detailTris[(detail.triBase + t) * 4];
                glm::vec3 corners[3];
                for (int k = 0; k < 3; ++k) {
                    const unsigned char index = triangle[k];
                    corners[k] = (index < poly.vertCount)
                        ? to_vec3(&tile->verts[poly.verts[index] * 3])
                        : to_vec3(&tile->detailVerts[(detail.vertBase + (index - poly.vertCount)) * 3]);
                    m_detail_triangle_vertices.push_back(corners[k]);
                }
                area += triangle_area(corners[0], corners[1], corners[2]);
            }
            polygon.area = area;
            m_polygons.push_back(polygon);
        }
    }
}

void Navigation_mesh::clear_reachable()
{
    for (Navigation_polygon& polygon : m_polygons) {
        polygon.reachable = false;
    }
}

auto Navigation_mesh::mark_reachable(
    const std::span<const glm::vec3>     seeds,
    const glm::vec3&                     search_half_extents,
    std::vector<Navigation_seed_result>& out_results
) -> bool
{
    std::vector<uint8_t> reachable;
    const bool found = find_reachable(seeds, search_half_extents, out_results, reachable);
    set_reachable(reachable);
    return found;
}

void Navigation_mesh::set_reachable(const std::span<const uint8_t> reachable)
{
    for (std::size_t i = 0, end = m_polygons.size(); i < end; ++i) {
        m_polygons[i].reachable = (i < reachable.size()) && (reachable[i] != 0);
    }
}

auto Navigation_mesh::find_reachable(
    const std::span<const glm::vec3>     seeds,
    const glm::vec3&                     search_half_extents,
    std::vector<Navigation_seed_result>& out_results,
    std::vector<uint8_t>&                out_reachable
) const -> bool
{
    out_results.clear();
    out_reachable.assign(m_polygons.size(), uint8_t{0});

    std::unique_ptr<dtNavMeshQuery, Detour_query_deleter> query{dtAllocNavMeshQuery()};
    if (!query || dtStatusFailed(query->init(m_nav_mesh.get(), 2048))) {
        log_navigation->error("find_reachable: could not initialize the Detour query");
        for (const glm::vec3& seed : seeds) {
            Navigation_seed_result result{};
            result.seed   = seed;
            result.reason = "Detour query initialization failed";
            out_results.push_back(result);
        }
        return false;
    }

    const dtQueryFilter   filter{};
    std::vector<uint32_t> stack;
    std::size_t           found_count = 0;
    for (const glm::vec3& seed : seeds) {
        Navigation_seed_result result{};
        result.seed = seed;
        dtPolyRef ref = 0;
        float     nearest[3]{seed.x, seed.y, seed.z};
        const dtStatus status = query->findNearestPoly(&seed.x, &search_half_extents.x, &filter, &ref, nearest);
        if (dtStatusFailed(status) || (ref == 0)) {
            result.reason = fmt::format(
                "no polygon within {} m horizontally and {} m vertically",
                search_half_extents.x, search_half_extents.y
            );
            log_navigation->info("seed ({}, {}, {}): {}", seed.x, seed.y, seed.z, result.reason);
            out_results.push_back(result);
            continue;
        }
        unsigned int salt = 0;
        unsigned int slot = 0;
        unsigned int poly = 0;
        m_nav_mesh->decodePolyId(ref, salt, slot, poly);
        result.found   = true;
        result.polygon = m_slot_polygon_base[slot] + poly;
        result.point   = to_vec3(nearest);
        out_results.push_back(result);
        ++found_count;
        if (out_reachable[result.polygon] == 0) {
            out_reachable[result.polygon] = 1;
            stack.push_back(result.polygon);
        }
    }

    while (!stack.empty()) {
        const uint32_t polygon_index = stack.back();
        stack.pop_back();
        const Navigation_polygon& polygon = m_polygons[polygon_index];
        for (uint32_t i = 0; i < polygon.neighbour_count; ++i) {
            const uint32_t neighbour = m_neighbours[polygon.first_neighbour + i];
            if (out_reachable[neighbour] == 0) {
                out_reachable[neighbour] = 1;
                stack.push_back(neighbour);
            }
        }
    }

    if (found_count == 0) {
        std::string reasons;
        for (const Navigation_seed_result& result : out_results) {
            reasons += fmt::format(
                "{}seed ({}, {}, {}): {}",
                reasons.empty() ? "" : "; ", result.seed.x, result.seed.y, result.seed.z, result.reason
            );
        }
        log_navigation->warn("find_reachable: every seed failed: {}", reasons.empty() ? std::string{"no seeds"} : reasons);
        return false;
    }
    return true;
}

auto Navigation_mesh::get_detour_nav_mesh() const -> const dtNavMesh*
{
    return m_nav_mesh.get();
}

auto Navigation_mesh::get_settings() const -> const Navigation_build_settings&
{
    return m_settings;
}

auto Navigation_mesh::get_bounds_min() const -> const glm::vec3&
{
    return m_bounds_min;
}

auto Navigation_mesh::get_bounds_max() const -> const glm::vec3&
{
    return m_bounds_max;
}

auto Navigation_mesh::get_tile_count() const -> std::size_t
{
    return m_tile_count;
}

auto Navigation_mesh::get_vertices() const -> std::span<const glm::vec3>
{
    return m_vertices;
}

auto Navigation_mesh::get_polygons() const -> std::span<const Navigation_polygon>
{
    return m_polygons;
}

auto Navigation_mesh::get_polygons() -> std::span<Navigation_polygon>
{
    return m_polygons;
}

auto Navigation_mesh::get_polygon_vertex_indices() const -> std::span<const uint32_t>
{
    return m_polygon_vertex_indices;
}

auto Navigation_mesh::get_neighbours() const -> std::span<const uint32_t>
{
    return m_neighbours;
}

auto Navigation_mesh::get_detail_triangle_vertices() const -> std::span<const glm::vec3>
{
    return m_detail_triangle_vertices;
}

auto Navigation_mesh::get_totals() const -> Navigation_totals
{
    Navigation_totals totals{};
    totals.polygon_count = m_polygons.size();
    for (const Navigation_polygon& polygon : m_polygons) {
        totals.total_area += static_cast<double>(polygon.area);
        if (polygon.reachable) {
            ++totals.reachable_polygon_count;
            totals.reachable_area += static_cast<double>(polygon.area);
        }
    }
    return totals;
}

auto Navigation_mesh::get_polygon_centroid(const uint32_t polygon_index) const -> glm::vec3
{
    const Navigation_polygon& polygon = m_polygons[polygon_index];
    if (polygon.vertex_count == 0) {
        return glm::vec3{0.0f};
    }
    glm::vec3 sum{0.0f};
    for (uint32_t i = 0; i < polygon.vertex_count; ++i) {
        sum += m_vertices[m_polygon_vertex_indices[polygon.first_vertex + i]];
    }
    return sum / static_cast<float>(polygon.vertex_count);
}

} // namespace editor
