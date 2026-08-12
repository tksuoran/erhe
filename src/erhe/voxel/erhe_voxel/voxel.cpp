#include "erhe_voxel/voxel.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

#if defined(_MSC_VER)
// C4701 (potentially uninitialized local) fires inside OpenVDB's
// ConvexVoxelizer.h when its templates are instantiated by this TU
// (createLevelSetTaperedCapsule). It is a codegen-time warning raised at
// the instantiation point, so it cannot be push/pop-scoped around the
// includes; disable it for this whole wrapper TU.
#   pragma warning(disable : 4701)
#endif

#include <openvdb/openvdb.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/GridTransformer.h>
#include <openvdb/tools/Interpolation.h>
#include <openvdb/tools/LevelSetFilter.h>
#include <openvdb/tools/LevelSetMeasure.h>
#include <openvdb/tools/LevelSetSphere.h>
#include <openvdb/tools/LevelSetTubes.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>

#include <mutex>
#include <vector>

namespace erhe::voxel {

namespace {

// openvdb::initialize() is idempotent but not free; run it once.
void ensure_openvdb_initialized()
{
    static std::once_flag once{};
    std::call_once(once, [](){ openvdb::initialize(); });
}

[[nodiscard]] auto make_transform(const float voxel_size) -> openvdb::math::Transform::Ptr
{
    return openvdb::math::Transform::createLinearTransform(static_cast<double>(voxel_size));
}

} // anonymous namespace

class Grid_impl
{
public:
    Grid_impl(openvdb::FloatGrid::Ptr grid, const int narrow_band_width)
        : m_grid             {grid}
        , m_narrow_band_width{narrow_band_width}
    {
        ERHE_VERIFY(m_grid);
        m_grid->setGridClass(openvdb::GRID_LEVEL_SET);
    }

    [[nodiscard]] auto grid() const -> openvdb::FloatGrid::Ptr
    {
        return m_grid;
    }
    [[nodiscard]] auto narrow_band_width() const -> int
    {
        return m_narrow_band_width;
    }
    [[nodiscard]] auto voxel_size() const -> float
    {
        return static_cast<float>(m_grid->voxelSize().x());
    }

private:
    openvdb::FloatGrid::Ptr m_grid;
    int                     m_narrow_band_width;
};

Grid::Grid(const Grid_create_info& create_info)
{
    ensure_openvdb_initialized();
    const float background = create_info.voxel_size * static_cast<float>(create_info.narrow_band_width);
    openvdb::FloatGrid::Ptr grid = openvdb::FloatGrid::create(background);
    grid->setTransform(make_transform(create_info.voxel_size));
    m_impl = std::make_unique<Grid_impl>(grid, create_info.narrow_band_width);
}

Grid::Grid(std::unique_ptr<Grid_impl>&& impl)
    : m_impl{std::move(impl)}
{
}

Grid::Grid(const Grid& other)
    : m_impl{
        std::make_unique<Grid_impl>(
            openvdb::deepCopyTypedGrid<openvdb::FloatGrid>(other.m_impl->grid()),
            other.m_impl->narrow_band_width()
        )
    }
{
}

auto Grid::operator=(const Grid& other) -> Grid&
{
    if (this != &other) {
        m_impl = std::make_unique<Grid_impl>(
            openvdb::deepCopyTypedGrid<openvdb::FloatGrid>(other.m_impl->grid()),
            other.m_impl->narrow_band_width()
        );
    }
    return *this;
}

Grid::Grid(Grid&& old) noexcept = default;
auto Grid::operator=(Grid&& old) noexcept -> Grid& = default;
Grid::~Grid() noexcept = default;

auto Grid::make_sphere(const Grid_create_info& create_info, const glm::vec3 center, const float radius) -> Grid
{
    ensure_openvdb_initialized();
    openvdb::FloatGrid::Ptr grid = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
        radius,
        openvdb::Vec3f{center.x, center.y, center.z},
        create_info.voxel_size,
        static_cast<float>(create_info.narrow_band_width)
    );
    return Grid{std::make_unique<Grid_impl>(grid, create_info.narrow_band_width)};
}

auto Grid::make_capsule(
    const Grid_create_info& create_info,
    const glm::vec3         p0,
    const glm::vec3         p1,
    const float             radius0,
    const float             radius1
) -> Grid
{
    ensure_openvdb_initialized();
    openvdb::FloatGrid::Ptr grid = openvdb::tools::createLevelSetTaperedCapsule<openvdb::FloatGrid>(
        openvdb::Vec3f{p0.x, p0.y, p0.z},
        openvdb::Vec3f{p1.x, p1.y, p1.z},
        radius0,
        radius1,
        create_info.voxel_size,
        static_cast<float>(create_info.narrow_band_width)
    );
    return Grid{std::make_unique<Grid_impl>(grid, create_info.narrow_band_width)};
}

auto Grid::from_geometry(const Grid_create_info& create_info, const erhe::geometry::Geometry& geometry) -> Grid
{
    ensure_openvdb_initialized();

    const GEO::Mesh& mesh = geometry.get_mesh();

    std::vector<openvdb::Vec3s> points;
    points.reserve(mesh.vertices.nb());
    for (GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
        points.emplace_back(p.x, p.y, p.z);
    }

    std::vector<openvdb::Vec3I> triangles;
    triangles.reserve(erhe::geometry::count_mesh_facet_triangles(mesh));
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
        const GEO::index_t v0 = mesh.facets.vertex(facet, 0);
        for (GEO::index_t i = 1; i + 1 < corner_count; ++i) {
            const GEO::index_t v1 = mesh.facets.vertex(facet, i);
            const GEO::index_t v2 = mesh.facets.vertex(facet, i + 1);
            triangles.emplace_back(
                static_cast<std::uint32_t>(v0),
                static_cast<std::uint32_t>(v1),
                static_cast<std::uint32_t>(v2)
            );
        }
    }

    openvdb::FloatGrid::Ptr grid = openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(
        *make_transform(create_info.voxel_size),
        points,
        triangles,
        static_cast<float>(create_info.narrow_band_width)
    );
    return Grid{std::make_unique<Grid_impl>(grid, create_info.narrow_band_width)};
}

void Grid::to_geometry(erhe::geometry::Geometry& destination, const float adaptivity) const
{
    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec3I> triangles;
    std::vector<openvdb::Vec4I> quads;
    openvdb::tools::volumeToMesh(
        *m_impl->grid(),
        points,
        triangles,
        quads,
        0.0,
        static_cast<double>(adaptivity)
    );

    GEO::Mesh& mesh = destination.get_mesh();
    mesh.vertices.create_vertices(static_cast<GEO::index_t>(points.size()));
    for (std::size_t i = 0; i < points.size(); ++i) {
        const openvdb::Vec3s& p = points[i];
        erhe::geometry::set_pointf(
            mesh.vertices,
            static_cast<GEO::index_t>(i),
            GEO::vec3f{p.x(), p.y(), p.z()}
        );
    }

    // volumeToMesh() emits faces wound for the OpenVDB convention; reverse
    // the corner order so facet normals point outward in erhe's
    // counter-clockwise-out convention (same flip PicoGK applies).
    for (const openvdb::Vec3I& triangle : triangles) {
        mesh.facets.create_triangle(
            static_cast<GEO::index_t>(triangle[2]),
            static_cast<GEO::index_t>(triangle[1]),
            static_cast<GEO::index_t>(triangle[0])
        );
    }
    for (const openvdb::Vec4I& quad : quads) {
        mesh.facets.create_quad(
            static_cast<GEO::index_t>(quad[3]),
            static_cast<GEO::index_t>(quad[2]),
            static_cast<GEO::index_t>(quad[1]),
            static_cast<GEO::index_t>(quad[0])
        );
    }
    mesh.facets.connect();
}

void Grid::union_with(const Grid& other)
{
    ERHE_VERIFY(get_voxel_size() == other.get_voxel_size());
    openvdb::FloatGrid::Ptr operand = openvdb::deepCopyTypedGrid<openvdb::FloatGrid>(other.m_impl->grid());
    openvdb::tools::csgUnion(*m_impl->grid(), *operand);
}

void Grid::subtract(const Grid& other)
{
    ERHE_VERIFY(get_voxel_size() == other.get_voxel_size());
    openvdb::FloatGrid::Ptr operand = openvdb::deepCopyTypedGrid<openvdb::FloatGrid>(other.m_impl->grid());
    openvdb::tools::csgDifference(*m_impl->grid(), *operand);
}

void Grid::intersect(const Grid& other)
{
    ERHE_VERIFY(get_voxel_size() == other.get_voxel_size());
    openvdb::FloatGrid::Ptr operand = openvdb::deepCopyTypedGrid<openvdb::FloatGrid>(other.m_impl->grid());
    openvdb::tools::csgIntersection(*m_impl->grid(), *operand);
}

void Grid::offset(const float distance)
{
    openvdb::tools::LevelSetFilter<openvdb::FloatGrid> filter{*m_impl->grid()};
    // OpenVDB level set offsets are positive-inward; negate so that a
    // positive distance grows the surface outward.
    filter.offset(-distance);
}

void Grid::smooth(const int iterations)
{
    openvdb::tools::LevelSetFilter<openvdb::FloatGrid> filter{*m_impl->grid()};
    for (int i = 0; i < iterations; ++i) {
        filter.gaussian();
    }
}

auto Grid::is_empty() const -> bool
{
    if (m_impl->grid()->tree().empty()) {
        return true;
    }
    // 0 values can be left over after boolean operations; a grid with no
    // value below 0 contains no interior and is fundamentally empty
    // (same check PicoGK uses).
    for (openvdb::FloatGrid::ValueOnCIter iter = m_impl->grid()->cbeginValueOn(); iter.test(); ++iter) {
        if (*iter < 0.0f) {
            return false;
        }
    }
    return true;
}

auto Grid::get_voxel_size() const -> float
{
    return m_impl->voxel_size();
}

auto Grid::get_background() const -> float
{
    return m_impl->grid()->background();
}

auto Grid::sample(const glm::vec3 position) const -> float
{
    const openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler> sampler{*m_impl->grid()};
    return sampler.wsSample(
        openvdb::Vec3R{
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z)
        }
    );
}

auto Grid::get_volume() const -> float
{
    if (is_empty()) {
        return 0.0f;
    }
    return static_cast<float>(openvdb::tools::levelSetVolume(*m_impl->grid(), true));
}

auto Grid::get_active_voxel_count() const -> std::int64_t
{
    return static_cast<std::int64_t>(m_impl->grid()->activeVoxelCount());
}

auto Grid::get_memory_usage() const -> std::int64_t
{
    return static_cast<std::int64_t>(m_impl->grid()->memUsage());
}

auto Grid::get_aabb() const -> erhe::math::Aabb
{
    erhe::math::Aabb aabb{};
    const openvdb::CoordBBox coord_bbox = m_impl->grid()->evalActiveVoxelBoundingBox();
    if (coord_bbox.empty()) {
        return aabb;
    }
    const openvdb::BBoxd world_bbox = m_impl->grid()->transform().indexToWorld(coord_bbox);
    aabb.include(
        glm::vec3{
            static_cast<float>(world_bbox.min().x()),
            static_cast<float>(world_bbox.min().y()),
            static_cast<float>(world_bbox.min().z())
        }
    );
    aabb.include(
        glm::vec3{
            static_cast<float>(world_bbox.max().x()),
            static_cast<float>(world_bbox.max().y()),
            static_cast<float>(world_bbox.max().z())
        }
    );
    return aabb;
}

} // namespace erhe::voxel
