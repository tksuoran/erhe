#include "navigation/navigation_build.hpp"
#include "navigation/navigation_mesh.hpp"
#include "editor_log.hpp"

#include "erhe_task/task.hpp"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace editor {

namespace {

// Tolerance for meter-to-cell rounding: 1.8 m / 0.1 m is 18 cells, not 19.
constexpr double c_cell_epsilon = 1.0e-4;

[[nodiscard]] auto cells_round_up(const double meters, const double cell) -> int
{
    return static_cast<int>(std::ceil((meters / cell) - c_cell_epsilon));
}

[[nodiscard]] auto cells_round_down(const double meters, const double cell) -> int
{
    return static_cast<int>(std::floor((meters / cell) + c_cell_epsilon));
}

[[nodiscard]] auto cells_round_nearest(const double meters, const double cell) -> int
{
    return static_cast<int>(std::lround(meters / cell));
}

[[nodiscard]] auto next_pow2(unsigned int v) -> unsigned int
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

[[nodiscard]] auto ilog2(const unsigned int v) -> unsigned int
{
    unsigned int r = 0;
    while ((v >> (r + 1)) != 0) {
        ++r;
    }
    return r;
}

// Forwards Recast's log to the editor's navigation category.
class Navigation_recast_context : public rcContext
{
protected:
    void doLog(const rcLogCategory category, const char* msg, const int len) override
    {
        const std::string_view message{msg, static_cast<std::size_t>(std::max(len, 0))};
        switch (category) {
            case RC_LOG_ERROR:    log_navigation->error("Recast: {}", message); break;
            case RC_LOG_WARNING:  log_navigation->warn ("Recast: {}", message); break;
            case RC_LOG_PROGRESS:
            default:              log_navigation->trace("Recast: {}", message); break;
        }
    }
};

class Heightfield_deleter         { public: void operator()(rcHeightfield*        p) const { rcFreeHeightField(p);        } };
class Compact_heightfield_deleter { public: void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); } };
class Contour_set_deleter         { public: void operator()(rcContourSet*         p) const { rcFreeContourSet(p);         } };
class Poly_mesh_deleter           { public: void operator()(rcPolyMesh*           p) const { rcFreePolyMesh(p);           } };
class Poly_mesh_detail_deleter    { public: void operator()(rcPolyMeshDetail*     p) const { rcFreePolyMeshDetail(p);     } };

class Tile_result
{
public:
    unsigned char* data     {nullptr}; // dtAlloc'd; null for a tile without walkable spans
    int            data_size{0};
    std::string    error;
};

class Tile_job
{
public:
    int                   tile_x{0};
    int                   tile_y{0};
    std::vector<int>      triangles; // input triangle indices overlapping the grown tile
};

class Build_input
{
public:
    std::vector<float> vertices;  // x, y, z
    std::vector<int>   triangles; // 3 vertex indices each
};

void build_tile(
    const Build_input& build_input,
    const rcConfig&    base_config,
    const Navigation_build_settings& settings,
    const glm::vec3&   bounds_min,
    const glm::vec3&   bounds_max,
    const Tile_job&    job,
    Tile_result&       result
)
{
    Navigation_recast_context context;
    rcConfig config = base_config;
    const float tile_world_size = static_cast<float>(config.tileSize) * config.cs;
    const float border_world    = static_cast<float>(config.borderSize) * config.cs;
    config.bmin[0] = bounds_min.x + (static_cast<float>(job.tile_x) * tile_world_size) - border_world;
    config.bmin[1] = bounds_min.y;
    config.bmin[2] = bounds_min.z + (static_cast<float>(job.tile_y) * tile_world_size) - border_world;
    config.bmax[0] = bounds_min.x + (static_cast<float>(job.tile_x + 1) * tile_world_size) + border_world;
    config.bmax[1] = bounds_max.y;
    config.bmax[2] = bounds_min.z + (static_cast<float>(job.tile_y + 1) * tile_world_size) + border_world;

    const std::string tile_name = fmt::format("tile ({}, {})", job.tile_x, job.tile_y);

    std::unique_ptr<rcHeightfield, Heightfield_deleter> heightfield{rcAllocHeightfield()};
    if (!heightfield || !rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch)) {
        result.error = fmt::format("{}: could not create the heightfield", tile_name);
        return;
    }

    const int*  vertices_triangles = build_input.triangles.data();
    const int   vertex_count       = static_cast<int>(build_input.vertices.size() / 3);
    std::vector<int>           tile_triangles;
    std::vector<unsigned char> areas;
    tile_triangles.reserve(job.triangles.size() * 3);
    for (const int triangle : job.triangles) {
        const int* t = &vertices_triangles[static_cast<std::size_t>(triangle) * 3];
        tile_triangles.push_back(t[0]);
        tile_triangles.push_back(t[1]);
        tile_triangles.push_back(t[2]);
    }
    const int tile_triangle_count = static_cast<int>(job.triangles.size());
    areas.resize(job.triangles.size(), RC_NULL_AREA);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, build_input.vertices.data(), vertex_count, tile_triangles.data(), tile_triangle_count, areas.data());
    if (!rcRasterizeTriangles(&context, build_input.vertices.data(), vertex_count, tile_triangles.data(), areas.data(), tile_triangle_count, *heightfield, config.walkableClimb)) {
        result.error = fmt::format("{}: could not rasterize the triangles", tile_name);
        return;
    }

    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans                 (&context, config.walkableHeight, config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans     (&context, config.walkableHeight, *heightfield);

    std::unique_ptr<rcCompactHeightfield, Compact_heightfield_deleter> compact{rcAllocCompactHeightfield()};
    if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact)) {
        result.error = fmt::format("{}: could not build the compact heightfield", tile_name);
        return;
    }
    heightfield.reset();
    if (compact->spanCount == 0) {
        return;
    }
    if ((config.walkableRadius > 0) && !rcErodeWalkableArea(&context, config.walkableRadius, *compact)) {
        result.error = fmt::format("{}: could not erode the walkable area", tile_name);
        return;
    }

    switch (settings.partitioning) {
        case Navigation_partitioning::watershed: {
            if (!rcBuildDistanceField(&context, *compact)) {
                result.error = fmt::format("{}: could not build the distance field", tile_name);
                return;
            }
            if (!rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea)) {
                result.error = fmt::format("{}: could not build watershed regions", tile_name);
                return;
            }
            break;
        }
        case Navigation_partitioning::monotone: {
            if (!rcBuildRegionsMonotone(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea)) {
                result.error = fmt::format("{}: could not build monotone regions", tile_name);
                return;
            }
            break;
        }
        case Navigation_partitioning::layers:
        default: {
            if (!rcBuildLayerRegions(&context, *compact, config.borderSize, config.minRegionArea)) {
                result.error = fmt::format("{}: could not build layer regions", tile_name);
                return;
            }
            break;
        }
    }

    std::unique_ptr<rcContourSet, Contour_set_deleter> contours{rcAllocContourSet()};
    if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours)) {
        result.error = fmt::format("{}: could not build the contours", tile_name);
        return;
    }
    if (contours->nconts == 0) {
        return;
    }

    std::unique_ptr<rcPolyMesh, Poly_mesh_deleter> poly_mesh{rcAllocPolyMesh()};
    if (!poly_mesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *poly_mesh)) {
        result.error = fmt::format("{}: could not build the polygon mesh", tile_name);
        return;
    }
    std::unique_ptr<rcPolyMeshDetail, Poly_mesh_detail_deleter> detail{rcAllocPolyMeshDetail()};
    if (!detail || !rcBuildPolyMeshDetail(&context, *poly_mesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detail)) {
        result.error = fmt::format("{}: could not build the detail mesh", tile_name);
        return;
    }
    if (poly_mesh->npolys == 0) {
        return;
    }
    if (poly_mesh->nverts >= 0xffff) {
        result.error = fmt::format("{}: {} polygon mesh vertices, Detour takes at most {}; lower tile_size", tile_name, poly_mesh->nverts, 0xffff);
        return;
    }
    for (int i = 0; i < poly_mesh->npolys; ++i) {
        poly_mesh->flags[i] = (poly_mesh->areas[i] == RC_WALKABLE_AREA) ? 1 : 0;
    }

    dtNavMeshCreateParams params{};
    params.verts            = poly_mesh->verts;
    params.vertCount        = poly_mesh->nverts;
    params.polys            = poly_mesh->polys;
    params.polyAreas        = poly_mesh->areas;
    params.polyFlags        = poly_mesh->flags;
    params.polyCount        = poly_mesh->npolys;
    params.nvp              = poly_mesh->nvp;
    params.detailMeshes     = detail->meshes;
    params.detailVerts      = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris       = detail->tris;
    params.detailTriCount   = detail->ntris;
    params.walkableHeight   = settings.agent_height;
    params.walkableRadius   = settings.agent_radius;
    params.walkableClimb    = settings.agent_max_climb;
    params.tileX            = job.tile_x;
    params.tileY            = job.tile_y;
    params.tileLayer        = 0;
    rcVcopy(params.bmin, poly_mesh->bmin);
    rcVcopy(params.bmax, poly_mesh->bmax);
    params.cs               = config.cs;
    params.ch               = config.ch;
    params.buildBvTree      = true;

    unsigned char* data      = nullptr;
    int            data_size = 0;
    if (!dtCreateNavMeshData(&params, &data, &data_size)) {
        result.error = fmt::format("{}: could not create the Detour tile data", tile_name);
        return;
    }
    result.data      = data;
    result.data_size = data_size;
}

[[nodiscard]] auto fail(std::string& out_error, std::string message) -> std::unique_ptr<Navigation_mesh>
{
    log_navigation->error("build_navigation_mesh: {}", message);
    out_error = std::move(message);
    return {};
}

} // anonymous namespace

auto make_recast_config(const Navigation_build_settings& settings) -> rcConfig
{
    const double cs = static_cast<double>(settings.cell_size);
    const double ch = static_cast<double>(settings.cell_height);
    rcConfig config{};
    config.cs                     = settings.cell_size;
    config.ch                     = settings.cell_height;
    config.walkableSlopeAngle     = settings.agent_max_slope;
    config.walkableHeight         = cells_round_up  (static_cast<double>(settings.agent_height),    ch);
    config.walkableClimb          = cells_round_down(static_cast<double>(settings.agent_max_climb), ch);
    config.walkableRadius         = cells_round_up  (static_cast<double>(settings.agent_radius),    cs);
    config.maxEdgeLen             = cells_round_down(static_cast<double>(settings.edge_max_length), cs);
    config.maxSimplificationError = settings.edge_max_error;
    const int region_min_cells    = cells_round_nearest(static_cast<double>(settings.region_min_size),   cs);
    const int region_merge_cells  = cells_round_nearest(static_cast<double>(settings.region_merge_size), cs);
    config.minRegionArea          = region_min_cells * region_min_cells;
    config.mergeRegionArea        = region_merge_cells * region_merge_cells;
    config.maxVertsPerPoly        = settings.verts_per_poly;
    config.tileSize               = std::max(1, cells_round_nearest(static_cast<double>(settings.tile_size), cs));
    config.borderSize             = config.walkableRadius + 3;
    config.width                  = config.tileSize + (2 * config.borderSize);
    config.height                 = config.tileSize + (2 * config.borderSize);
    config.detailSampleDist       = ((static_cast<double>(settings.detail_sample_distance) / cs) < 0.9) ? 0.0f : settings.detail_sample_distance;
    config.detailSampleMaxError   = settings.detail_sample_max_error;
    return config;
}

auto build_navigation_mesh(
    const Navigation_input&          input,
    const Navigation_build_settings& settings,
    tf::Executor&                    executor,
    const Navigation_build_progress& progress,
    std::string&                     out_error
) -> std::unique_ptr<Navigation_mesh>
{
    out_error.clear();

    if ((settings.cell_size <= 0.0f) || (settings.cell_height <= 0.0f)) {
        return fail(out_error, "cell_size and cell_height must be positive");
    }
    if ((settings.agent_max_slope < 0.0f) || (settings.agent_max_slope >= 90.0f)) {
        return fail(out_error, fmt::format("agent_max_slope {} is outside [0, 90)", settings.agent_max_slope));
    }
    if ((settings.verts_per_poly < 3) || (settings.verts_per_poly > DT_VERTS_PER_POLYGON)) {
        return fail(out_error, fmt::format("verts_per_poly {} is outside [3, {}]", settings.verts_per_poly, DT_VERTS_PER_POLYGON));
    }
    if ((settings.agent_height <= 0.0f) || (settings.agent_radius < 0.0f) || (settings.agent_max_climb < 0.0f)) {
        return fail(out_error, "agent_height must be positive, agent_radius and agent_max_climb not negative");
    }
    const rcConfig base_config = make_recast_config(settings);
    if (base_config.walkableHeight < 3) {
        return fail(out_error, fmt::format("agent_height is {} cells; Recast needs at least 3", base_config.walkableHeight));
    }
    if (base_config.walkableRadius >= 255) {
        return fail(out_error, fmt::format("agent_radius is {} cells; Recast takes less than 255", base_config.walkableRadius));
    }

    // Flatten the input into Recast's form.
    Build_input build_input;
    std::size_t total_vertex_count   = 0;
    std::size_t total_triangle_count = 0;
    for (const Navigation_input_mesh& mesh : input.meshes) {
        total_vertex_count   += mesh.positions.size();
        total_triangle_count += mesh.triangle_indices.size() / 3;
    }
    if ((total_vertex_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) || (total_triangle_count > static_cast<std::size_t>(std::numeric_limits<int>::max() / 3))) {
        return fail(out_error, "input exceeds Recast's int vertex / triangle indices");
    }
    build_input.vertices.reserve(total_vertex_count * 3);
    build_input.triangles.reserve(total_triangle_count * 3);
    for (std::size_t mesh_index = 0; mesh_index < input.meshes.size(); ++mesh_index) {
        const Navigation_input_mesh& mesh = input.meshes[mesh_index];
        if ((mesh.triangle_indices.size() % 3) != 0) {
            return fail(out_error, fmt::format("input mesh {}: {} triangle indices is not a multiple of 3", mesh_index, mesh.triangle_indices.size()));
        }
        const int vertex_base = static_cast<int>(build_input.vertices.size() / 3);
        for (const glm::vec3& position : mesh.positions) {
            build_input.vertices.push_back(position.x);
            build_input.vertices.push_back(position.y);
            build_input.vertices.push_back(position.z);
        }
        for (const uint32_t index : mesh.triangle_indices) {
            if (index >= mesh.positions.size()) {
                return fail(out_error, fmt::format("input mesh {}: triangle index {} is past its {} positions", mesh_index, index, mesh.positions.size()));
            }
            build_input.triangles.push_back(vertex_base + static_cast<int>(index));
        }
    }
    const std::size_t triangle_count = build_input.triangles.size() / 3;
    if (triangle_count == 0) {
        return fail(out_error, "the input has no triangles");
    }

    // Bounds: the settings' region, or the union of the input bounds.
    glm::vec3 bounds_min = settings.region_min;
    glm::vec3 bounds_max = settings.region_max;
    if ((bounds_min == glm::vec3{0.0f}) && (bounds_max == glm::vec3{0.0f})) {
        bounds_min = glm::vec3{std::numeric_limits<float>::max()};
        bounds_max = glm::vec3{std::numeric_limits<float>::lowest()};
        for (const int index : build_input.triangles) {
            const glm::vec3 p{
                build_input.vertices[(static_cast<std::size_t>(index) * 3) + 0],
                build_input.vertices[(static_cast<std::size_t>(index) * 3) + 1],
                build_input.vertices[(static_cast<std::size_t>(index) * 3) + 2]
            };
            bounds_min = glm::min(bounds_min, p);
            bounds_max = glm::max(bounds_max, p);
        }
    }
    if (!(bounds_max.x > bounds_min.x) || !(bounds_max.z > bounds_min.z) || !(bounds_max.y >= bounds_min.y)) {
        return fail(
            out_error,
            fmt::format(
                "bounds ({}, {}, {}) .. ({}, {}, {}) are empty",
                bounds_min.x, bounds_min.y, bounds_min.z, bounds_max.x, bounds_max.y, bounds_max.z
            )
        );
    }
    if (((bounds_max.y - bounds_min.y) / settings.cell_height) > static_cast<float>(RC_SPAN_MAX_HEIGHT)) {
        return fail(out_error, fmt::format("bounds height {} m exceeds {} cells of cell_height", bounds_max.y - bounds_min.y, RC_SPAN_MAX_HEIGHT));
    }

    // Tile grid.
    int grid_width  = 0;
    int grid_height = 0;
    rcCalcGridSize(&bounds_min.x, &bounds_max.x, base_config.cs, &grid_width, &grid_height);
    const int tile_size    = base_config.tileSize;
    const int tiles_x      = (grid_width  + tile_size - 1) / tile_size;
    const int tiles_y      = (grid_height + tile_size - 1) / tile_size;
    const int tile_count   = tiles_x * tiles_y;
    const int max_tile_bits = 14;
    if (tile_count > (1 << max_tile_bits)) {
        return fail(out_error, fmt::format("{} x {} tiles exceed Detour's {} tiles of 32-bit polygon references; raise tile_size", tiles_x, tiles_y, 1 << max_tile_bits));
    }
    const int tile_bits = std::min(static_cast<int>(ilog2(next_pow2(static_cast<unsigned int>(tile_count)))), max_tile_bits);
    const int poly_bits = 22 - tile_bits;

    // Per tile, the triangles whose bounds reach into the tile grown by the border.
    const float tile_world_size = static_cast<float>(tile_size) * base_config.cs;
    const float border_world    = static_cast<float>(base_config.borderSize) * base_config.cs;
    std::vector<Tile_job> jobs(static_cast<std::size_t>(tile_count));
    for (int y = 0; y < tiles_y; ++y) {
        for (int x = 0; x < tiles_x; ++x) {
            Tile_job& job = jobs[static_cast<std::size_t>((y * tiles_x) + x)];
            job.tile_x = x;
            job.tile_y = y;
        }
    }
    for (std::size_t t = 0; t < triangle_count; ++t) {
        float min_x = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float min_z = std::numeric_limits<float>::max();
        float max_z = std::numeric_limits<float>::lowest();
        for (int k = 0; k < 3; ++k) {
            const std::size_t v = static_cast<std::size_t>(build_input.triangles[(t * 3) + static_cast<std::size_t>(k)]) * 3;
            min_x = std::min(min_x, build_input.vertices[v + 0]);
            max_x = std::max(max_x, build_input.vertices[v + 0]);
            min_z = std::min(min_z, build_input.vertices[v + 2]);
            max_z = std::max(max_z, build_input.vertices[v + 2]);
        }
        const int x0 = static_cast<int>(std::floor((min_x - border_world - bounds_min.x) / tile_world_size));
        const int x1 = static_cast<int>(std::floor((max_x + border_world - bounds_min.x) / tile_world_size));
        const int y0 = static_cast<int>(std::floor((min_z - border_world - bounds_min.z) / tile_world_size));
        const int y1 = static_cast<int>(std::floor((max_z + border_world - bounds_min.z) / tile_world_size));
        if ((x1 < 0) || (y1 < 0) || (x0 >= tiles_x) || (y0 >= tiles_y)) {
            continue;
        }
        for (int y = std::max(y0, 0), y_end = std::min(y1, tiles_y - 1); y <= y_end; ++y) {
            for (int x = std::max(x0, 0), x_end = std::min(x1, tiles_x - 1); x <= x_end; ++x) {
                jobs[static_cast<std::size_t>((y * tiles_x) + x)].triangles.push_back(static_cast<int>(t));
            }
        }
    }

    // One task per tile; each writes only its own result slot.
    std::vector<Tile_result> results(jobs.size());
    std::mutex               progress_mutex;
    std::size_t              tiles_done = 0;
    const std::size_t        tiles_total = jobs.size();
    {
        tf::Taskflow taskflow;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            taskflow.emplace(
                [&, i]() {
                    if (!jobs[i].triangles.empty()) {
                        build_tile(build_input, base_config, settings, bounds_min, bounds_max, jobs[i], results[i]);
                    }
                    const std::lock_guard<std::mutex> lock{progress_mutex};
                    ++tiles_done;
                    if (progress) {
                        progress(tiles_done, tiles_total);
                    }
                }
            );
        }
        erhe::task::run(executor, taskflow).wait();
    }

    const auto free_results = [&results]() {
        for (Tile_result& result : results) {
            if (result.data != nullptr) {
                dtFree(result.data);
                result.data = nullptr;
            }
        }
    };
    for (const Tile_result& result : results) {
        if (!result.error.empty()) {
            free_results();
            return fail(out_error, result.error);
        }
    }

    std::unique_ptr<dtNavMesh, Detour_nav_mesh_deleter> nav_mesh{dtAllocNavMesh()};
    dtNavMeshParams params{};
    params.orig[0]    = bounds_min.x;
    params.orig[1]    = bounds_min.y;
    params.orig[2]    = bounds_min.z;
    params.tileWidth  = tile_world_size;
    params.tileHeight = tile_world_size;
    params.maxTiles   = 1 << tile_bits;
    params.maxPolys   = 1 << poly_bits;
    if (!nav_mesh || dtStatusFailed(nav_mesh->init(&params))) {
        free_results();
        return fail(out_error, "Detour navigation mesh initialization failed");
    }
    for (std::size_t i = 0; i < results.size(); ++i) {
        Tile_result& result = results[i];
        if (result.data == nullptr) {
            continue;
        }
        const dtStatus status = nav_mesh->addTile(result.data, result.data_size, DT_TILE_FREE_DATA, 0, nullptr);
        if (dtStatusFailed(status)) {
            free_results();
            return fail(
                out_error,
                fmt::format(
                    "Detour refused tile ({}, {}) (status 0x{:x}; at most {} polygons per tile)",
                    jobs[i].tile_x, jobs[i].tile_y, status, 1 << poly_bits
                )
            );
        }
        result.data = nullptr; // the navigation mesh owns it now
    }

    std::unique_ptr<Navigation_mesh> navigation_mesh = std::make_unique<Navigation_mesh>(std::move(nav_mesh), settings, bounds_min, bounds_max);
    navigation_mesh->fill_from_detour();
    if (navigation_mesh->get_polygons().empty()) {
        return fail(out_error, "the build produced no walkable polygon");
    }
    log_navigation->info(
        "built navigation mesh: {} x {} tiles ({} with polygons), {} polygons, {} triangles in",
        tiles_x, tiles_y, navigation_mesh->get_tile_count(), navigation_mesh->get_polygons().size(), triangle_count
    );
    return navigation_mesh;
}

} // namespace editor
