#include "erhe_geometry/operation/lattice_deform.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <algorithm>
#include <cmath>

namespace erhe::geometry::operation {

namespace {

// Structural flags, plus smooth vertex normal regeneration when requested.
// Unlike remesh, facet texture coordinates are never regenerated: topology
// and parametrization are unchanged by the deformation, only normals are
// invalidated by the non-affine map.
[[nodiscard]] auto lattice_process_flags(const bool regenerate_attributes) -> uint64_t
{
    uint64_t flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids;
    if (regenerate_attributes) {
        flags |= Geometry::process_flag_compute_smooth_vertex_normals;
    }
    return flags;
}

// All degree-n Bernstein basis values at t, via the de Casteljau style
// recurrence (numerically robust at any degree, no explicit binomials).
void bernstein_weights(const int degree, const float t, std::vector<double>& weights)
{
    weights.assign(static_cast<std::size_t>(degree) + 1, 0.0);
    weights[0] = 1.0;
    const double td = static_cast<double>(t);
    const double ud = 1.0 - td;
    for (int j = 1; j <= degree; ++j) {
        for (int i = j; i >= 1; --i) {
            weights[static_cast<std::size_t>(i)] = ud * weights[static_cast<std::size_t>(i)] + td * weights[static_cast<std::size_t>(i - 1)];
        }
        weights[0] = ud * weights[0];
    }
}

class Lattice_deform : public Geometry_operation
{
public:
    Lattice_deform(const Geometry& source, Geometry& destination, const Lattice_deform_parameters& parameters)
        : Geometry_operation{source, destination}
        , m_parameters      {parameters}
    {
    }

    void build();

private:
    [[nodiscard]] auto offset_index(int i, int j, int k) const -> std::size_t
    {
        return lattice_offset_index(m_parameters.divisions, i, j, k);
    }

    [[nodiscard]] auto trilinear_displacement(glm::vec3 stu) const -> glm::vec3;
    [[nodiscard]] auto bezier_displacement   (glm::vec3 stu) const -> glm::vec3;
    void               add_cage_debug_lines  () const;

    const Lattice_deform_parameters& m_parameters;

    // Scratch for bezier_displacement(), reused across vertices
    mutable std::vector<double> m_weights_s;
    mutable std::vector<double> m_weights_t;
    mutable std::vector<double> m_weights_u;
};

auto Lattice_deform::trilinear_displacement(const glm::vec3 stu) const -> glm::vec3
{
    const glm::ivec3& d = m_parameters.divisions;
    glm::ivec3 cell;
    glm::vec3  fraction;
    for (int axis = 0; axis < 3; ++axis) {
        const float scaled = stu[axis] * static_cast<float>(d[axis]);
        const int   i      = std::min(static_cast<int>(scaled), d[axis] - 1);
        cell    [axis] = i;
        fraction[axis] = scaled - static_cast<float>(i);
    }
    const std::vector<glm::vec3>& offsets = m_parameters.control_point_offsets;
    glm::vec3 displacement{0.0f};
    for (int corner = 0; corner < 8; ++corner) {
        const int di = (corner     ) & 1;
        const int dj = (corner >> 1) & 1;
        const int dk = (corner >> 2) & 1;
        const float weight =
            ((di == 1) ? fraction.x : 1.0f - fraction.x) *
            ((dj == 1) ? fraction.y : 1.0f - fraction.y) *
            ((dk == 1) ? fraction.z : 1.0f - fraction.z);
        displacement += weight * offsets[offset_index(cell.x + di, cell.y + dj, cell.z + dk)];
    }
    return displacement;
}

auto Lattice_deform::bezier_displacement(const glm::vec3 stu) const -> glm::vec3
{
    const glm::ivec3& d = m_parameters.divisions;
    bernstein_weights(d.x, stu.x, m_weights_s);
    bernstein_weights(d.y, stu.y, m_weights_t);
    bernstein_weights(d.z, stu.z, m_weights_u);
    const std::vector<glm::vec3>& offsets = m_parameters.control_point_offsets;
    glm::dvec3 displacement{0.0};
    for (int k = 0; k <= d.z; ++k) {
        for (int j = 0; j <= d.y; ++j) {
            const double weight_tu = m_weights_t[static_cast<std::size_t>(j)] * m_weights_u[static_cast<std::size_t>(k)];
            for (int i = 0; i <= d.x; ++i) {
                const double weight = m_weights_s[static_cast<std::size_t>(i)] * weight_tu;
                displacement += weight * glm::dvec3{offsets[offset_index(i, j, k)]};
            }
        }
    }
    return glm::vec3{displacement};
}

void Lattice_deform::add_cage_debug_lines() const
{
    const glm::ivec3& d      = m_parameters.divisions;
    const glm::vec3   extent = m_parameters.cage_max - m_parameters.cage_min;
    const glm::vec4   color{1.0f, 0.6f, 0.1f, 1.0f};

    const auto control_point = [&](int i, int j, int k) -> glm::vec3 {
        const glm::vec3 rest =
            m_parameters.cage_min +
            extent * glm::vec3{
                static_cast<float>(i) / static_cast<float>(d.x),
                static_cast<float>(j) / static_cast<float>(d.y),
                static_cast<float>(k) / static_cast<float>(d.z)
            };
        return glm::vec3{m_parameters.cage_transform * glm::vec4{rest + m_parameters.control_point_offsets[offset_index(i, j, k)], 1.0f}};
    };

    for (int k = 0; k <= d.z; ++k) {
        for (int j = 0; j <= d.y; ++j) {
            for (int i = 0; i <= d.x; ++i) {
                const glm::vec3 p = control_point(i, j, k);
                if (i < d.x) { destination.add_debug_line(GEO::NO_INDEX, GEO::NO_INDEX, p, control_point(i + 1, j, k), color, 1.0f); }
                if (j < d.y) { destination.add_debug_line(GEO::NO_INDEX, GEO::NO_INDEX, p, control_point(i, j + 1, k), color, 1.0f); }
                if (k < d.z) { destination.add_debug_line(GEO::NO_INDEX, GEO::NO_INDEX, p, control_point(i, j, k + 1), color, 1.0f); }
            }
        }
    }
}

void Lattice_deform::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination.get_attributes().bind();
    copy_mesh_attributes();

    const glm::ivec3& d      = m_parameters.divisions;
    const glm::vec3   extent = m_parameters.cage_max - m_parameters.cage_min;

    const bool divisions_valid = (d.x >= 1) && (d.y >= 1) && (d.z >= 1);
    const bool offsets_valid   = divisions_valid && (m_parameters.control_point_offsets.size() == lattice_control_point_count(d));
    const bool extent_valid    = (extent.x > 0.0f) && (extent.y > 0.0f) && (extent.z > 0.0f);
    const bool transform_valid = std::abs(glm::determinant(m_parameters.cage_transform)) > 1e-12f;
    if (!offsets_valid || !extent_valid || !transform_valid) {
        log_operation->warn(
            "lattice_deform: invalid parameters (divisions {} {} {}, {} offsets, extent {} {} {}, cage transform det {}), passing geometry through unchanged",
            d.x, d.y, d.z, m_parameters.control_point_offsets.size(), extent.x, extent.y, extent.z,
            glm::determinant(m_parameters.cage_transform)
        );
        post_processing(lattice_process_flags(false));
        return;
    }

    // The cage box and offsets live in cage space; vertices map into it to
    // find their lattice coordinates and the interpolated offset rotates
    // back through the transform's linear part.
    const bool      has_cage_transform = (m_parameters.cage_transform != glm::mat4{1.0f});
    const glm::mat4 cage_from_local    = has_cage_transform ? glm::inverse(m_parameters.cage_transform) : glm::mat4{1.0f};
    const glm::mat3 cage_linear        {m_parameters.cage_transform};

    for (GEO::index_t vertex : destination_mesh.vertices) {
        const GEO::vec3f p = get_pointf(destination_mesh.vertices, vertex);
        const glm::vec3  position{p.x, p.y, p.z};
        const glm::vec3  position_cage = has_cage_transform
            ? glm::vec3{cage_from_local * glm::vec4{position, 1.0f}}
            : position;
        const glm::vec3  stu = glm::clamp((position_cage - m_parameters.cage_min) / extent, glm::vec3{0.0f}, glm::vec3{1.0f});
        const glm::vec3  displacement_cage = (m_parameters.interpolation == Lattice_interpolation::bezier)
            ? bezier_displacement(stu)
            : trilinear_displacement(stu);
        const glm::vec3  displacement = has_cage_transform ? (cage_linear * displacement_cage) : displacement_cage;
        const glm::vec3  deformed = position + displacement;
        set_pointf(destination_mesh.vertices, vertex, GEO::vec3f{deformed.x, deformed.y, deformed.z});
    }

    if (m_parameters.make_cage_debug_lines) {
        add_cage_debug_lines();
    }

    post_processing(lattice_process_flags(m_parameters.regenerate_attributes));
}

} // anonymous namespace

auto lattice_control_point_count(const glm::ivec3& divisions) -> std::size_t
{
    return
        static_cast<std::size_t>(divisions.x + 1) *
        static_cast<std::size_t>(divisions.y + 1) *
        static_cast<std::size_t>(divisions.z + 1);
}

auto lattice_offset_index(const glm::ivec3& divisions, const int i, const int j, const int k) -> std::size_t
{
    return
        static_cast<std::size_t>(i) +
        static_cast<std::size_t>(divisions.x + 1) * (static_cast<std::size_t>(j) + static_cast<std::size_t>(divisions.y + 1) * static_cast<std::size_t>(k));
}

void lattice_deform(const Geometry& source, Geometry& destination, const Lattice_deform_parameters& parameters)
{
    Lattice_deform operation{source, destination, parameters};
    operation.build();
}

} // namespace erhe::geometry::operation
