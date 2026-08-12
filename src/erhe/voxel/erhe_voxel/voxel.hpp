#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace erhe::geometry { class Geometry; }

namespace erhe::voxel {

class Grid_impl;

// PicoGK-style level-set contract (doc/openvdb-integration-plan.md):
// uniform voxel size, axis-aligned linear transform, narrow-band signed
// distance values clamped to +/- background, GRID_LEVEL_SET grid class.
class Grid_create_info
{
public:
    float voxel_size       {0.05f};
    int   narrow_band_width{3};     // half width of the narrow band, in voxels
};

// Sparse narrow-band signed distance field over an OpenVDB FloatGrid.
// Wraps the grid behind a pimpl so that consumers do not pull in the
// OpenVDB headers.
class Grid final
{
public:
    explicit Grid(const Grid_create_info& create_info); // empty level set
    Grid           (const Grid& other);                 // deep copy
    auto operator= (const Grid& other) -> Grid&;
    Grid           (Grid&& old) noexcept;
    auto operator= (Grid&& old) noexcept -> Grid&;
    ~Grid          () noexcept;

    [[nodiscard]] static auto make_sphere(
        const Grid_create_info& create_info,
        glm::vec3               center,
        float                   radius
    ) -> Grid;

    // Capsule / tapered cone beam from p0 (radius0) to p1 (radius1)
    [[nodiscard]] static auto make_capsule(
        const Grid_create_info& create_info,
        glm::vec3               p0,
        glm::vec3               p1,
        float                   radius0,
        float                   radius1
    ) -> Grid;

    // Voxelize a closed triangle/polygon mesh into a signed distance field.
    // Polygonal facets are fan-triangulated.
    [[nodiscard]] static auto from_geometry(
        const Grid_create_info&         create_info,
        const erhe::geometry::Geometry& geometry
    ) -> Grid;

    // Extract the zero isosurface into (an empty) destination Geometry as
    // quad-dominant polygon facets. adaptivity in [0, 1]: 0 = uniform quads,
    // higher values simplify flat regions.
    void to_geometry(erhe::geometry::Geometry& destination, float adaptivity = 0.0f) const;

    // Boolean operations; the operand grid is not modified.
    // Both grids must share the same voxel size.
    void union_with(const Grid& other);
    void subtract  (const Grid& other);
    void intersect (const Grid& other);

    // Move the surface outward (positive distance, world units) or inward
    // (negative).
    void offset(float distance);

    // Gaussian level-set smoothing.
    void smooth(int iterations);

    [[nodiscard]] auto is_empty              () const -> bool;
    [[nodiscard]] auto get_voxel_size        () const -> float;
    [[nodiscard]] auto get_background        () const -> float; // narrow band half width in world units
    [[nodiscard]] auto sample                (glm::vec3 position) const -> float; // trilinear signed distance, clamped to +/- background
    [[nodiscard]] auto get_volume            () const -> float; // world units cubed
    [[nodiscard]] auto get_active_voxel_count() const -> std::int64_t;
    [[nodiscard]] auto get_memory_usage      () const -> std::int64_t; // bytes
    [[nodiscard]] auto get_aabb              () const -> erhe::math::Aabb; // world units, empty if grid is empty

private:
    explicit Grid(std::unique_ptr<Grid_impl>&& impl);

    std::unique_ptr<Grid_impl> m_impl;
};

} // namespace erhe::voxel
