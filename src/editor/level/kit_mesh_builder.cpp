#include "level/kit_mesh_builder.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <unordered_map>

namespace editor {

Kit_random::Kit_random(const int seed)
    : m_state{static_cast<uint64_t>(static_cast<uint32_t>(seed)) ^ 0x6a09e667f3bcc909ull}
{
}

Kit_random::Kit_random(const int seed, const std::string_view stream_name)
    : Kit_random{seed}
{
    // FNV-1a 64 of the name.
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const char c : stream_name) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001b3ull;
    }
    m_state ^= hash;
}

auto Kit_random::next_u32() -> uint32_t
{
    m_state += 0x9e3779b97f4a7c15ull;
    uint64_t z = m_state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z = z ^ (z >> 31);
    return static_cast<uint32_t>(z >> 32);
}

auto Kit_random::next_unit() -> float
{
    // 24 bits scaled by a power of two: exact in float on every platform.
    return static_cast<float>(next_u32() >> 8) * (1.0f / 16777216.0f);
}

auto Kit_random::next_range(const float min_value, const float max_value) -> float
{
    return min_value + ((max_value - min_value) * next_unit());
}

auto Kit_mesh_builder::add_vertex(const glm::vec3& position) -> uint32_t
{
    m_positions.push_back(position);
    return static_cast<uint32_t>(m_positions.size() - 1);
}

void Kit_mesh_builder::set_slot(const uint32_t slot)
{
    m_slot = slot;
}

void Kit_mesh_builder::add_triangle(const uint32_t a, const uint32_t b, const uint32_t c)
{
    m_facets.push_back({a, b, c});
    m_facet_slots.push_back(m_slot);
}

void Kit_mesh_builder::add_quad(const uint32_t a, const uint32_t b, const uint32_t c, const uint32_t d)
{
    m_facets.push_back({a, b, c, d});
    m_facet_slots.push_back(m_slot);
}

void Kit_mesh_builder::add_polygon(const std::span<const uint32_t> corners)
{
    m_facets.emplace_back(corners.begin(), corners.end());
    m_facet_slots.push_back(m_slot);
}

void Kit_mesh_builder::add_box(const glm::vec3& lo, const glm::vec3& hi)
{
    const uint32_t v0 = add_vertex({lo.x, lo.y, lo.z});
    const uint32_t v1 = add_vertex({hi.x, lo.y, lo.z});
    const uint32_t v2 = add_vertex({lo.x, hi.y, lo.z});
    const uint32_t v3 = add_vertex({lo.x, lo.y, hi.z});
    const uint32_t v4 = add_vertex({hi.x, hi.y, lo.z});
    const uint32_t v5 = add_vertex({hi.x, lo.y, hi.z});
    const uint32_t v6 = add_vertex({lo.x, hi.y, hi.z});
    const uint32_t v7 = add_vertex({hi.x, hi.y, hi.z});
    add_quad(v1, v4, v7, v5); // +x
    add_quad(v2, v6, v7, v4); // +y
    add_quad(v3, v5, v7, v6); // +z
    add_quad(v0, v3, v6, v2); // -x
    add_quad(v0, v1, v5, v3); // -y
    add_quad(v0, v2, v4, v1); // -z
}

namespace {

auto ring_point(const float radius, const float y, const float angle) -> glm::vec3
{
    return glm::vec3{radius * std::cos(angle), y, -radius * std::sin(angle)};
}

} // anonymous namespace

void Kit_mesh_builder::add_prism(const int segments, const float radius, const float y0, const float y1, const float phase_radians)
{
    const uint32_t n     = static_cast<uint32_t>(segments);
    const uint32_t base  = static_cast<uint32_t>(m_positions.size());
    const float    step  = 2.0f * std::numbers::pi_v<float> / static_cast<float>(segments);
    for (uint32_t i = 0; i < n; ++i) {
        add_vertex(ring_point(radius, y0, phase_radians + (step * static_cast<float>(i))));
    }
    for (uint32_t i = 0; i < n; ++i) {
        add_vertex(ring_point(radius, y1, phase_radians + (step * static_cast<float>(i))));
    }
    std::vector<uint32_t> top;
    std::vector<uint32_t> bottom;
    for (uint32_t i = 0; i < n; ++i) {
        top.push_back(base + n + i);
        bottom.push_back(base + (n - 1 - i));
    }
    add_polygon(top);
    add_polygon(bottom);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        add_quad(base + i, base + j, base + n + j, base + n + i);
    }
}

namespace {

// The rings' points and their bands, two triangles per quad; returns the first vertex index.
auto add_ring_bands(Kit_mesh_builder& builder, const std::span<const glm::vec3> ring_points, const uint32_t n) -> uint32_t
{
    const uint32_t ring_count = static_cast<uint32_t>(ring_points.size()) / n;
    uint32_t base = 0;
    for (std::size_t i = 0; i < ring_points.size(); ++i) {
        const uint32_t v = builder.add_vertex(ring_points[i]);
        if (i == 0) {
            base = v;
        }
    }
    for (uint32_t r = 0; r + 1 < ring_count; ++r) {
        const uint32_t a = base + (r * n);
        const uint32_t b = a + n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            builder.add_triangle(a + i, a + j, b + j);
            builder.add_triangle(a + i, b + j, b + i);
        }
    }
    return base;
}

} // anonymous namespace

void Kit_mesh_builder::add_ring_solid_paired_caps(const std::span<const glm::vec3> ring_points, const int segments)
{
    ERHE_VERIFY((segments >= 4) && ((segments % 2) == 0) && ((ring_points.size() % static_cast<std::size_t>(segments)) == 0) && (ring_points.size() >= 2 * static_cast<std::size_t>(segments)));
    const uint32_t n    = static_cast<uint32_t>(segments);
    const uint32_t base = add_ring_bands(*this, ring_points, n);
    const uint32_t last = base + (static_cast<uint32_t>(ring_points.size()) - n);
    for (uint32_t i = 0; i + 1 < n / 2; ++i) {
        add_quad(base + (n - 1 - i), base + (n - 2 - i), base + i + 1, base + i);
        add_quad(last + i, last + i + 1, last + (n - 2 - i), last + (n - 1 - i));
    }
}

void Kit_mesh_builder::add_ring_solid(const std::span<const glm::vec3> ring_points, const int segments, const glm::vec3& bottom_point, const glm::vec3& top_point)
{
    ERHE_VERIFY((segments >= 3) && ((ring_points.size() % static_cast<std::size_t>(segments)) == 0) && (ring_points.size() >= static_cast<std::size_t>(segments)));
    const uint32_t n      = static_cast<uint32_t>(segments);
    const uint32_t base   = add_ring_bands(*this, ring_points, n);
    const uint32_t last   = base + (static_cast<uint32_t>(ring_points.size()) - n);
    const uint32_t bottom = add_vertex(bottom_point);
    const uint32_t top    = add_vertex(top_point);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        add_triangle(bottom, base + j, base + i);
        add_triangle(last + i, last + j, top);
    }
}

void Kit_mesh_builder::add_x_prism(const float x0, const float x1, const std::span<const glm::vec2> section, const std::span<const std::vector<uint32_t>> cap_pieces)
{
    ERHE_VERIFY((x0 < x1) && (section.size() >= 3));
    const uint32_t n    = static_cast<uint32_t>(section.size());
    const uint32_t base = static_cast<uint32_t>(m_positions.size());
    for (const glm::vec2& p : section) {
        add_vertex(glm::vec3{x0, p.y, p.x});
    }
    for (const glm::vec2& p : section) {
        add_vertex(glm::vec3{x1, p.y, p.x});
    }
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        add_quad(base + i, base + j, base + n + j, base + n + i);
    }
    std::vector<uint32_t> corners;
    for (const std::vector<uint32_t>& piece : cap_pieces) {
        corners.clear();
        for (const uint32_t index : piece) {
            corners.push_back(base + n + index);
        }
        add_polygon(corners);
        corners.clear();
        for (std::size_t k = piece.size(); k > 0; --k) {
            corners.push_back(base + piece[k - 1]);
        }
        add_polygon(corners);
    }
}

void Kit_mesh_builder::add_mesh(const GEO::Mesh& mesh)
{
    const uint32_t base = static_cast<uint32_t>(m_positions.size());
    for (GEO::index_t v = 0, end = mesh.vertices.nb(); v < end; ++v) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, v);
        add_vertex(glm::vec3{p.x, p.y, p.z});
    }
    for (GEO::index_t f = 0, end = mesh.facets.nb(); f < end; ++f) {
        const GEO::index_t corner_count = mesh.facets.nb_vertices(f);
        if (corner_count == 4) {
            const uint32_t a = base + mesh.facets.vertex(f, 0);
            const uint32_t b = base + mesh.facets.vertex(f, 1);
            const uint32_t c = base + mesh.facets.vertex(f, 2);
            const uint32_t d = base + mesh.facets.vertex(f, 3);
            add_triangle(a, b, c);
            add_triangle(a, c, d);
            continue;
        }
        std::vector<uint32_t>& facet = m_facets.emplace_back();
        for (GEO::index_t c = 0; c < corner_count; ++c) {
            facet.push_back(base + mesh.facets.vertex(f, c));
        }
        m_facet_slots.push_back(m_slot);
    }
}

void Kit_mesh_builder::add_beam(const glm::vec3& a, const glm::vec3& b, const float width, const float height)
{
    ERHE_VERIFY(glm::length(b - a) > 0.0f); // a beam has a length
    const glm::vec3 direction = glm::normalize(b - a);
    const glm::vec3 reference = (std::abs(direction.y) > 0.99f) ? glm::vec3{0.0f, 0.0f, -1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 side      = (0.5f * width)  * glm::normalize(glm::cross(direction, reference));
    const glm::vec3 up        = (0.5f * height) * glm::normalize(glm::cross(side, direction));
    // (axis, up, side) is right-handed like (x, y, z), so the corners and quads follow add_box().
    const uint32_t v0 = add_vertex(a - up - side);
    const uint32_t v1 = add_vertex(b - up - side);
    const uint32_t v2 = add_vertex(a + up - side);
    const uint32_t v3 = add_vertex(a - up + side);
    const uint32_t v4 = add_vertex(b + up - side);
    const uint32_t v5 = add_vertex(b - up + side);
    const uint32_t v6 = add_vertex(a + up + side);
    const uint32_t v7 = add_vertex(b + up + side);
    add_quad(v1, v4, v7, v5);
    add_quad(v2, v6, v7, v4);
    add_quad(v3, v5, v7, v6);
    add_quad(v0, v3, v6, v2);
    add_quad(v0, v1, v5, v3);
    add_quad(v0, v2, v4, v1);
}

void Kit_mesh_builder::add_band(
    const std::span<const uint32_t> first,
    const std::span<const float>    first_keys,
    const std::span<const uint32_t> second,
    const std::span<const float>    second_keys
)
{
    std::size_t i = 0;
    std::size_t j = 0;
    while (((i + 1) < first.size()) || ((j + 1) < second.size())) {
        const bool advance_first = ((j + 1) >= second.size()) || (((i + 1) < first.size()) && (first_keys[i + 1] <= second_keys[j + 1]));
        if (advance_first) {
            add_triangle(first[i], first[i + 1], second[j]);
            ++i;
        } else {
            add_triangle(first[i], second[j + 1], second[j]);
            ++j;
        }
    }
}

auto get_kit_corner_angle(const glm::vec3& previous, const glm::vec3& corner, const glm::vec3& next) -> float
{
    const glm::vec3 a       = previous - corner;
    const glm::vec3 b       = next - corner;
    const float     lengths = glm::length(a) * glm::length(b);
    return (lengths > 0.0f) ? std::acos(std::clamp(glm::dot(a, b) / lengths, -1.0f, 1.0f)) : 0.0f;
}

namespace {

[[nodiscard]] auto make_edge_key(const uint32_t a, const uint32_t b) -> uint64_t
{
    return (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint64_t>(std::max(a, b));
}

[[nodiscard]] auto make_directed_key(const uint32_t from, const uint32_t to) -> uint64_t
{
    return (static_cast<uint64_t>(from) << 32) | static_cast<uint64_t>(to);
}

} // anonymous namespace

void Kit_mesh_builder::add_hard_edge(const uint32_t a, const uint32_t b)
{
    ERHE_VERIFY(a != b);
    m_hard_edges.push_back(make_edge_key(a, b));
}

void Kit_mesh_builder::set_smooth_from(const uint32_t first)
{
    ERHE_VERIFY(first <= m_positions.size());
    m_smooth_ranges.push_back(std::array<uint32_t, 2>{first, static_cast<uint32_t>(m_positions.size())});
}

void Kit_mesh_builder::add_hard_loop(const uint32_t first, const uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        add_hard_edge(first + i, first + ((i + 1) % count));
    }
}

void Kit_mesh_builder::write(GEO::Mesh& mesh) const
{
    const GEO::index_t first_facet  = mesh.facets.nb();
    const GEO::index_t first_vertex = mesh.vertices.create_vertices(static_cast<GEO::index_t>(m_positions.size()));
    for (std::size_t i = 0, end = m_positions.size(); i < end; ++i) {
        const glm::vec3& p = m_positions[i];
        erhe::geometry::set_pointf(mesh.vertices, first_vertex + static_cast<GEO::index_t>(i), GEO::vec3f{p.x, p.y, p.z});
    }
    GEO::Attribute<GEO::index_t> slots{mesh.facets.attributes(), c_kit_slot_attribute};
    // Facets are made in runs of equal corner count: one resize of the mesh's attribute stores per run, not per facet.
    std::size_t i = 0;
    while (i < m_facets.size()) {
        const std::size_t corner_count = m_facets[i].size();
        std::size_t       run_end      = i + 1;
        while ((run_end < m_facets.size()) && (m_facets[run_end].size() == corner_count)) {
            ++run_end;
        }
        const GEO::index_t first = mesh.facets.create_facets(static_cast<GEO::index_t>(run_end - i), static_cast<GEO::index_t>(corner_count));
        for (std::size_t j = i; j < run_end; ++j) {
            const GEO::index_t f = first + static_cast<GEO::index_t>(j - i);
            for (std::size_t c = 0; c < corner_count; ++c) {
                mesh.facets.set_vertex(f, static_cast<GEO::index_t>(c), first_vertex + static_cast<GEO::index_t>(m_facets[j][c]));
            }
            slots[f] = static_cast<GEO::index_t>(m_facet_slots[j]);
        }
        i = run_end;
    }
    if (m_hard_edges.empty() && m_smooth_ranges.empty()) {
        return;
    }

    // Corner normals around the vertices of the hard edges and the smooth vertices: each corner averages, weighted by
    // their angles at the vertex, the facets reached by walking around the vertex both ways from its own facet up to a
    // hard edge.
    std::vector<uint64_t> hard_edges = m_hard_edges;
    std::sort(hard_edges.begin(), hard_edges.end());
    const auto is_hard = [&hard_edges](const uint32_t a, const uint32_t b) -> bool {
        return std::binary_search(hard_edges.begin(), hard_edges.end(), make_edge_key(a, b));
    };
    std::vector<bool> builder_shaded(m_positions.size(), false);
    for (const uint64_t key : hard_edges) {
        builder_shaded[static_cast<std::size_t>(key >> 32)]         = true;
        builder_shaded[static_cast<std::size_t>(key & 0xffffffffu)] = true;
    }
    for (const std::array<uint32_t, 2>& range : m_smooth_ranges) {
        for (uint32_t v = range[0]; v < range[1]; ++v) {
            builder_shaded[v] = true;
        }
    }
    std::unordered_map<uint64_t, uint32_t> facet_of_directed_edge;
    std::vector<glm::vec3>                 facet_normals(m_facets.size());
    for (std::size_t f = 0, end = m_facets.size(); f < end; ++f) {
        const std::vector<uint32_t>& corners = m_facets[f];
        glm::vec3 newell{0.0f};
        for (std::size_t c = 0, count = corners.size(); c < count; ++c) {
            const glm::vec3& p = m_positions[corners[c]];
            const glm::vec3& q = m_positions[corners[(c + 1) % count]];
            newell += glm::vec3{(p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)};
            facet_of_directed_edge[make_directed_key(corners[c], corners[(c + 1) % count])] = static_cast<uint32_t>(f);
        }
        const float length = glm::length(newell);
        facet_normals[f] = (length > 0.0f) ? (newell / length) : glm::vec3{0.0f};
    }
    const auto local_corner_of = [this](const uint32_t facet, const uint32_t vertex) -> std::size_t {
        const std::vector<uint32_t>& corners = m_facets[facet];
        return static_cast<std::size_t>(std::find(corners.begin(), corners.end(), vertex) - corners.begin());
    };
    const auto weighted_normal = [this, &facet_normals](const uint32_t facet, const std::size_t c) -> glm::vec3 {
        const std::vector<uint32_t>& corners = m_facets[facet];
        const std::size_t            count   = corners.size();
        const float angle = get_kit_corner_angle(
            m_positions[corners[(c + count - 1) % count]], m_positions[corners[c]], m_positions[corners[(c + 1) % count]]
        );
        return angle * facet_normals[facet];
    };

    erhe::geometry::Attribute_present<GEO::vec3f> corner_normal{
        mesh.facet_corners.attributes(), erhe::geometry::Attribute_descriptors::s_normal
    };
    enum class Walk : unsigned int { forward = 0, backward };
    for (std::size_t f = 0, end = m_facets.size(); f < end; ++f) {
        const std::vector<uint32_t>& corners = m_facets[f];
        for (std::size_t c = 0, count = corners.size(); c < count; ++c) {
            const uint32_t vertex = corners[c];
            if (!builder_shaded[vertex]) {
                continue;
            }
            glm::vec3 sum    = weighted_normal(static_cast<uint32_t>(f), c);
            bool      closed = false;
            for (const Walk walk : {Walk::forward, Walk::backward}) {
                if (closed) {
                    break;
                }
                uint32_t    facet = static_cast<uint32_t>(f);
                std::size_t local = c;
                for (std::size_t step = 0; step < m_facets.size(); ++step) {
                    // Forward crosses the facet's edge leaving the vertex, backward the edge arriving at it.
                    const std::vector<uint32_t>& ring  = m_facets[facet];
                    const std::size_t            n     = ring.size();
                    const uint32_t               other = (walk == Walk::forward) ? ring[(local + 1) % n] : ring[(local + n - 1) % n];
                    if (is_hard(vertex, other)) {
                        break;
                    }
                    const uint64_t across = (walk == Walk::forward) ? make_directed_key(other, vertex) : make_directed_key(vertex, other);
                    const std::unordered_map<uint64_t, uint32_t>::const_iterator neighbor = facet_of_directed_edge.find(across);
                    if (neighbor == facet_of_directed_edge.end()) {
                        break;
                    }
                    if (neighbor->second == static_cast<uint32_t>(f)) {
                        closed = true;
                        break;
                    }
                    facet = neighbor->second;
                    local = local_corner_of(facet, vertex);
                    sum += weighted_normal(facet, local);
                }
            }
            const float     length = glm::length(sum);
            const glm::vec3 normal = (length > 0.0f) ? (sum / length) : facet_normals[f];
            corner_normal.set(mesh.facets.corner(first_facet + static_cast<GEO::index_t>(f), static_cast<GEO::index_t>(c)), GEO::vec3f{normal.x, normal.y, normal.z});
        }
    }
}

auto Kit_mesh_builder::get_positions() const -> const std::vector<glm::vec3>&
{
    return m_positions;
}

void read_kit_facet_slots(const GEO::Mesh& mesh, std::vector<uint32_t>& out_slots)
{
    out_slots.assign(mesh.facets.nb(), 0u);
    GEO::Attribute<GEO::index_t> slots;
    if (!slots.bind_if_is_defined(mesh.facets.attributes(), c_kit_slot_attribute)) {
        return;
    }
    for (GEO::index_t f = 0, end = mesh.facets.nb(); f < end; ++f) {
        out_slots[f] = static_cast<uint32_t>(slots[f]);
    }
}

void extract_kit_slot_mesh(const GEO::Mesh& source, const std::span<const uint32_t> facet_slots, const uint32_t slot, GEO::Mesh& out_mesh)
{
    std::vector<GEO::index_t> vertex_map(source.vertices.nb(), GEO::NO_INDEX);
    // The corner normals write() set at the hard edges go along; binding reads only, as the attribute is defined.
    GEO::AttributesManager& source_corner_attributes = const_cast<GEO::Mesh&>(source).facet_corners.attributes();
    const bool has_corner_normals = source_corner_attributes.is_defined(erhe::geometry::Attribute_descriptors::s_normal.present_name);
    std::optional<erhe::geometry::Attribute_present<GEO::vec3f>> source_normal;
    std::optional<erhe::geometry::Attribute_present<GEO::vec3f>> out_normal;
    if (has_corner_normals) {
        source_normal.emplace(source_corner_attributes, erhe::geometry::Attribute_descriptors::s_normal);
        out_normal.emplace(out_mesh.facet_corners.attributes(), erhe::geometry::Attribute_descriptors::s_normal);
    }
    for (GEO::index_t f = 0, end = source.facets.nb(); f < end; ++f) {
        if (facet_slots[f] != slot) {
            continue;
        }
        const GEO::index_t corner_count = source.facets.nb_vertices(f);
        const GEO::index_t new_facet    = out_mesh.facets.create_polygon(corner_count);
        for (GEO::index_t c = 0; c < corner_count; ++c) {
            const GEO::index_t v = source.facets.vertex(f, c);
            if (vertex_map[v] == GEO::NO_INDEX) {
                vertex_map[v] = out_mesh.vertices.create_vertex();
                erhe::geometry::set_pointf(out_mesh.vertices, vertex_map[v], erhe::geometry::get_pointf(source.vertices, v));
            }
            out_mesh.facets.set_vertex(new_facet, c, vertex_map[v]);
            if (has_corner_normals && source_normal->has(source.facets.corner(f, c))) {
                out_normal->set(out_mesh.facets.corner(new_facet, c), source_normal->get(source.facets.corner(f, c)));
            }
        }
    }
}

} // namespace editor
