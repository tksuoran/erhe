#include "level/kit_shape_checks.hpp"

#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace editor {

namespace {

auto read_positions(const GEO::Mesh& mesh) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> positions(mesh.vertices.nb());
    for (GEO::index_t v = 0, end = mesh.vertices.nb(); v < end; ++v) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, v);
        positions[v] = glm::vec3{p.x, p.y, p.z};
    }
    return positions;
}

auto facet_corners(const GEO::Mesh& mesh, const GEO::index_t facet) -> std::vector<uint32_t>
{
    std::vector<uint32_t> corners;
    for (GEO::index_t lv = 0, end = mesh.facets.nb_vertices(facet); lv < end; ++lv) {
        corners.push_back(mesh.facets.vertex(facet, lv));
    }
    return corners;
}

// Twice the area vector of a polygon (Newell), relative to its first corner so a small facet far from the origin keeps its precision.
auto newell_normal(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& corners) -> glm::vec3
{
    const glm::vec3 origin = positions[corners.front()];
    glm::vec3 sum{0.0f};
    for (std::size_t i = 0, n = corners.size(); i < n; ++i) {
        sum += glm::cross(positions[corners[i]] - origin, positions[corners[(i + 1) % n]] - origin);
    }
    return sum;
}

void get_bounds(const std::vector<glm::vec3>& positions, glm::vec3& out_min, glm::vec3& out_max)
{
    out_min = glm::vec3{0.0f};
    out_max = glm::vec3{0.0f};
    if (positions.empty()) {
        return;
    }
    out_min = positions.front();
    out_max = positions.front();
    for (const glm::vec3& p : positions) {
        out_min = glm::min(out_min, p);
        out_max = glm::max(out_max, p);
    }
}

auto bits_of(const glm::vec3& p) -> std::array<uint32_t, 3>
{
    std::array<uint32_t, 3> bits{};
    std::memcpy(bits.data(), &p, sizeof(float) * 3);
    return bits;
}

class Union_find
{
public:
    explicit Union_find(const std::size_t count) : m_parent(count)
    {
        std::iota(m_parent.begin(), m_parent.end(), std::size_t{0});
    }
    auto find(std::size_t i) -> std::size_t
    {
        while (m_parent[i] != i) {
            m_parent[i] = m_parent[m_parent[i]];
            i = m_parent[i];
        }
        return i;
    }
    void unite(const std::size_t a, const std::size_t b)
    {
        m_parent[find(a)] = find(b);
    }
private:
    std::vector<std::size_t> m_parent;
};

} // anonymous namespace

auto Kit_mesh_report::is_valid() const -> bool
{
    return problems.empty();
}

auto count_kit_triangles(const GEO::Mesh& mesh) -> unsigned int
{
    unsigned int count = 0;
    for (GEO::index_t f = 0, end = mesh.facets.nb(); f < end; ++f) {
        count += mesh.facets.nb_vertices(f) - 2;
    }
    return count;
}

auto check_kit_mesh(const GEO::Mesh& mesh) -> Kit_mesh_report
{
    Kit_mesh_report report;
    const std::vector<glm::vec3> positions = read_positions(mesh);
    report.vertex_count   = static_cast<unsigned int>(positions.size());
    report.facet_count    = mesh.facets.nb();
    report.triangle_count = count_kit_triangles(mesh);
    get_bounds(positions, report.bounds_min, report.bounds_max);
    const glm::vec3 extent_vector = report.bounds_max - report.bounds_min;
    const float     extent        = std::max(1.0f, std::max(extent_vector.x, std::max(extent_vector.y, extent_vector.z)));
    const float     length_tolerance = 1.0e-5f * extent;

    if ((report.vertex_count == 0) || (report.facet_count == 0)) {
        report.problems.push_back("mesh is empty");
        return report;
    }
    for (const glm::vec3& p : positions) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            report.problems.push_back("vertex position is not finite");
            return report;
        }
    }

    std::map<std::pair<uint32_t, uint32_t>, uint32_t> edge_facet;
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> vertex_fans(positions.size()); // (previous, next) per corner
    std::set<std::vector<uint32_t>> facet_sets;
    std::vector<uint32_t> vertex_use(positions.size(), 0);

    for (GEO::index_t f = 0; f < report.facet_count; ++f) {
        const std::vector<uint32_t> corners = facet_corners(mesh, f);
        const std::size_t           n       = corners.size();
        if (n < 3) {
            report.problems.push_back(fmt::format("facet {} has {} corners", f, n));
            continue;
        }
        std::vector<uint32_t> sorted = corners;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
            report.problems.push_back(fmt::format("facet {} uses a vertex twice", f));
            continue;
        }
        if (!facet_sets.insert(sorted).second) {
            report.problems.push_back(fmt::format("facet {} duplicates another facet", f));
        }

        const glm::vec3 normal = newell_normal(positions, corners);
        const float     area   = 0.5f * glm::length(normal);
        if (area <= 1.0e-9f * extent * extent) {
            report.problems.push_back(fmt::format("facet {} is degenerate (area {})", f, area));
            continue;
        }
        const glm::vec3 unit_normal = glm::normalize(normal);
        const glm::vec3 p0          = positions[corners[0]];
        for (std::size_t i = 0; i < n; ++i) {
            const glm::vec3 previous = positions[corners[(i + n - 1) % n]];
            const glm::vec3 current  = positions[corners[i]];
            const glm::vec3 next     = positions[corners[(i + 1) % n]];
            const glm::vec3 e0       = current - previous;
            const glm::vec3 e1       = next - current;
            const float     l0       = glm::length(e0);
            const float     l1       = glm::length(e1);
            if ((l0 <= length_tolerance) || (l1 <= length_tolerance)) {
                report.problems.push_back(fmt::format("facet {} has an edge shorter than {}", f, length_tolerance));
                break;
            }
            if (glm::dot(glm::cross(e0, e1), unit_normal) <= (1.0e-6f * l0 * l1)) {
                report.problems.push_back(fmt::format("facet {} is not strictly convex at corner {}", f, i));
                break;
            }
            if (std::abs(glm::dot(current - p0, unit_normal)) > length_tolerance) {
                report.problems.push_back(fmt::format("facet {} is not planar", f));
                break;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const uint32_t a = corners[i];
            const uint32_t b = corners[(i + 1) % n];
            ++vertex_use[a];
            if (!edge_facet.emplace(std::make_pair(a, b), f).second) {
                report.problems.push_back(fmt::format("edge {} -> {} is used twice in the same direction", a, b));
            }
            vertex_fans[a].emplace_back(corners[(i + n - 1) % n], b);
        }
    }

    for (const std::pair<const std::pair<uint32_t, uint32_t>, uint32_t>& entry : edge_facet) {
        if (!edge_facet.contains(std::make_pair(entry.first.second, entry.first.first))) {
            report.problems.push_back(fmt::format("edge {} -> {} is open", entry.first.first, entry.first.second));
            break;
        }
    }

    for (std::size_t v = 0, end = positions.size(); v < end; ++v) {
        if (vertex_use[v] == 0) {
            report.problems.push_back(fmt::format("vertex {} is unused", v));
            continue;
        }
        // Around a manifold vertex the corners form one cycle: each corner's next vertex is the following corner's previous.
        const std::vector<std::pair<uint32_t, uint32_t>>& fan = vertex_fans[v];
        uint32_t    current = fan.front().first;
        std::size_t steps   = 0;
        do {
            const std::vector<std::pair<uint32_t, uint32_t>>::const_iterator it = std::find_if(
                fan.begin(), fan.end(), [current](const std::pair<uint32_t, uint32_t>& c) { return c.first == current; }
            );
            if (it == fan.end()) {
                break;
            }
            current = it->second;
            ++steps;
        } while ((current != fan.front().first) && (steps <= fan.size()));
        if ((current != fan.front().first) || (steps != fan.size())) {
            report.problems.push_back(fmt::format("vertex {} is not manifold", v));
        }
    }

    std::set<std::array<uint32_t, 3>> position_bits;
    for (const glm::vec3& p : positions) {
        if (!position_bits.insert(bits_of(p)).second) {
            report.problems.push_back(fmt::format("two vertices share position ({}, {}, {})", p.x, p.y, p.z));
            break;
        }
    }

    Union_find components{report.facet_count};
    for (const std::pair<const std::pair<uint32_t, uint32_t>, uint32_t>& entry : edge_facet) {
        const std::map<std::pair<uint32_t, uint32_t>, uint32_t>::const_iterator twin = edge_facet.find(std::make_pair(entry.first.second, entry.first.first));
        if (twin != edge_facet.end()) {
            components.unite(entry.second, twin->second);
        }
    }
    std::map<std::size_t, double> component_volume;
    for (GEO::index_t f = 0; f < report.facet_count; ++f) {
        const std::vector<uint32_t> corners = facet_corners(mesh, f);
        double volume = 0.0;
        for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
            const glm::dvec3 a{positions[corners[0]]};
            const glm::dvec3 b{positions[corners[i]]};
            const glm::dvec3 c{positions[corners[i + 1]]};
            volume += glm::dot(a, glm::cross(b, c)) / 6.0;
        }
        component_volume[components.find(f)] += volume;
    }
    report.component_count = static_cast<unsigned int>(component_volume.size());
    for (const std::pair<const std::size_t, double>& entry : component_volume) {
        if (entry.second <= 0.0) {
            report.problems.push_back(fmt::format("a component has volume {}: winding is not outward", entry.second));
        }
    }
    return report;
}

auto check_kit_seam_side(const GEO::Mesh& mesh, const Kit_side side) -> std::string
{
    const std::vector<glm::vec3> positions = read_positions(mesh);
    glm::vec3 bounds_min{};
    glm::vec3 bounds_max{};
    get_bounds(positions, bounds_min, bounds_max);
    const int   axis      = static_cast<int>(side) / 2;
    const bool  positive  = (static_cast<int>(side) % 2) == 1;
    const float plane     = positive ? bounds_max[axis] : bounds_min[axis];
    const float sign      = positive ? 1.0f : -1.0f;
    const glm::vec3 size  = bounds_max - bounds_min;
    const float rectangle = size[(axis + 1) % 3] * size[(axis + 2) % 3];
    const float tolerance = 1.0e-5f * std::max(1.0f, std::max(size.x, std::max(size.y, size.z)));

    double covered = 0.0;
    for (GEO::index_t f = 0, end = mesh.facets.nb(); f < end; ++f) {
        const std::vector<uint32_t> corners = facet_corners(mesh, f);
        const bool on_plane = std::all_of(corners.begin(), corners.end(), [&](const uint32_t v) {
            return std::abs(positions[v][axis] - plane) <= tolerance;
        });
        if (!on_plane) {
            continue;
        }
        const glm::vec3 normal = newell_normal(positions, corners);
        if ((normal[axis] * sign) <= 0.0f) {
            continue;
        }
        covered += 0.5 * static_cast<double>(glm::length(normal));
    }
    if (std::abs(covered - static_cast<double>(rectangle)) > (1.0e-4 * static_cast<double>(rectangle))) {
        return fmt::format("seam side {}: facets cover {} of the side's {}", c_str(side), covered, rectangle);
    }
    return {};
}

auto check_kit_origin(const GEO::Mesh& mesh, const Kit_origin_rule rule) -> std::string
{
    const std::vector<glm::vec3> positions = read_positions(mesh);
    glm::vec3 bounds_min{};
    glm::vec3 bounds_max{};
    get_bounds(positions, bounds_min, bounds_max);
    const glm::vec3 size      = bounds_max - bounds_min;
    const float     tolerance = 1.0e-5f * std::max(1.0f, std::max(size.x, std::max(size.y, size.z)));
    if (std::abs(bounds_min.y) > tolerance) {
        return fmt::format("base is at y = {}, not 0", bounds_min.y);
    }
    glm::vec2  base_min{std::numeric_limits<float>::max()};
    glm::vec2  base_max{std::numeric_limits<float>::lowest()};
    glm::dvec2 base_sum{0.0};
    double     base_count{0.0};
    for (const glm::vec3& p : positions) {
        if (p.y <= tolerance) {
            base_min = glm::min(base_min, glm::vec2{p.x, p.z});
            base_max = glm::max(base_max, glm::vec2{p.x, p.z});
            base_sum += glm::dvec2{p.x, p.z};
            base_count += 1.0;
        }
    }
    if (rule == Kit_origin_rule::base_center) {
        const glm::vec2 center{base_sum / base_count};
        if ((std::abs(center.x) > tolerance) || (std::abs(center.y) > tolerance)) {
            return fmt::format("base center is at x = {}, z = {}, not at the origin", center.x, center.y);
        }
    } else {
        if ((base_min.x >= 0.0f) || (base_max.x <= 0.0f) || (base_min.y >= 0.0f) || (base_max.y <= 0.0f)) {
            return "the base does not contain the origin";
        }
    }
    return {};
}

auto kit_meshes_equal(const GEO::Mesh& lhs, const GEO::Mesh& rhs) -> bool
{
    if ((lhs.vertices.nb() != rhs.vertices.nb()) || (lhs.facets.nb() != rhs.facets.nb())) {
        return false;
    }
    for (GEO::index_t v = 0, end = lhs.vertices.nb(); v < end; ++v) {
        const GEO::vec3f a = erhe::geometry::get_pointf(lhs.vertices, v);
        const GEO::vec3f b = erhe::geometry::get_pointf(rhs.vertices, v);
        if (bits_of(glm::vec3{a.x, a.y, a.z}) != bits_of(glm::vec3{b.x, b.y, b.z})) {
            return false;
        }
    }
    for (GEO::index_t f = 0, end = lhs.facets.nb(); f < end; ++f) {
        if (facet_corners(lhs, f) != facet_corners(rhs, f)) {
            return false;
        }
    }
    return true;
}

auto check_kit_shape(const Kit_parameter_values& values) -> Kit_mesh_report
{
    const Kit_shape_definition* shape = values.get_shape();
    if ((shape == nullptr) || !shape->generator) {
        Kit_mesh_report report;
        report.problems.push_back("values have no shape with a generator");
        return report;
    }
    GEO::Mesh mesh{3, true};
    shape->generator(values, mesh);
    Kit_mesh_report report = check_kit_mesh(mesh);
    if (!report.problems.empty()) {
        return report;
    }
    if (shape->get_seam_sides) {
        for (const Kit_side side : shape->get_seam_sides(values)) {
            std::string problem = check_kit_seam_side(mesh, side);
            if (!problem.empty()) {
                report.problems.push_back(std::move(problem));
            }
        }
    }
    const Kit_origin_rule rule = (shape->randomness == Kit_randomness::seeded) ? Kit_origin_rule::base_contains_origin : Kit_origin_rule::base_center;
    std::string origin_problem = check_kit_origin(mesh, rule);
    if (!origin_problem.empty()) {
        report.problems.push_back(std::move(origin_problem));
    }
    return report;
}

} // namespace editor
