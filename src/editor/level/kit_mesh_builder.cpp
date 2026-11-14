#include "level/kit_mesh_builder.hpp"

#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>
#include <numbers>

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

void Kit_mesh_builder::add_blob(const Kit_blob& blob, Kit_random& random)
{
    const uint32_t n          = static_cast<uint32_t>(blob.segments);
    const uint32_t ring_count = static_cast<uint32_t>(blob.rings.size());
    const float    step       = 2.0f * std::numbers::pi_v<float> / static_cast<float>(blob.segments);

    std::vector<float> angle_factors(n);
    for (uint32_t i = 0; i < n; ++i) {
        angle_factors[i] = 1.0f + random.next_range(-blob.angle_jitter, blob.angle_jitter);
    }

    std::vector<uint32_t> ring_base(ring_count);
    for (uint32_t r = 0; r < ring_count; ++r) {
        const Kit_blob_ring& ring      = blob.rings[r];
        const bool           flat_ring =
            ((r == 0) && (blob.bottom_cap == Kit_blob_cap::flat)) ||
            ((r == ring_count - 1) && (blob.top_cap == Kit_blob_cap::flat));
        ring_base[r] = static_cast<uint32_t>(m_positions.size());
        for (uint32_t i = 0; i < n; ++i) {
            const float factor = angle_factors[i] * (1.0f + random.next_range(-blob.vertex_jitter, blob.vertex_jitter));
            const float dy     = random.next_range(-blob.height_jitter, blob.height_jitter);
            const float y      = flat_ring ? ring.y : (ring.y + dy);
            add_vertex(ring_point(ring.radius * factor, y, step * static_cast<float>(i)));
        }
    }

    for (uint32_t r = 0; r + 1 < ring_count; ++r) {
        const uint32_t a = ring_base[r];
        const uint32_t b = ring_base[r + 1];
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            add_triangle(a + i, a + j, b + j);
            add_triangle(a + i, b + j, b + i);
        }
    }

    const uint32_t bottom_ring = ring_base.front();
    const uint32_t top_ring    = ring_base.back();
    const auto apex_point = [&](const float y, const float radius) -> glm::vec3 {
        const float dx = random.next_range(-blob.apex_offset, blob.apex_offset) * radius;
        const float dz = random.next_range(-blob.apex_offset, blob.apex_offset) * radius;
        return glm::vec3{dx, y, dz};
    };

    const uint32_t bottom_center = (blob.bottom_cap == Kit_blob_cap::flat)
        ? add_vertex(glm::vec3{0.0f, blob.rings.front().y, 0.0f})
        : add_vertex(apex_point(blob.bottom_point_y, blob.rings.front().radius));
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        add_triangle(bottom_center, bottom_ring + j, bottom_ring + i);
    }

    const uint32_t top_center = (blob.top_cap == Kit_blob_cap::flat)
        ? add_vertex(glm::vec3{0.0f, blob.rings.back().y, 0.0f})
        : add_vertex(apex_point(blob.top_point_y, blob.rings.back().radius));
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        add_triangle(top_ring + i, top_ring + j, top_center);
    }
}

void Kit_mesh_builder::write(GEO::Mesh& mesh) const
{
    const GEO::index_t first_vertex = mesh.vertices.create_vertices(static_cast<GEO::index_t>(m_positions.size()));
    for (std::size_t i = 0, end = m_positions.size(); i < end; ++i) {
        const glm::vec3& p = m_positions[i];
        erhe::geometry::set_pointf(mesh.vertices, first_vertex + static_cast<GEO::index_t>(i), GEO::vec3f{p.x, p.y, p.z});
    }
    GEO::Attribute<GEO::index_t> slots{mesh.facets.attributes(), c_kit_slot_attribute};
    for (std::size_t i = 0, end = m_facets.size(); i < end; ++i) {
        const std::vector<uint32_t>& facet = m_facets[i];
        const GEO::index_t f = mesh.facets.create_polygon(static_cast<GEO::index_t>(facet.size()));
        for (std::size_t c = 0, corner_end = facet.size(); c < corner_end; ++c) {
            mesh.facets.set_vertex(f, static_cast<GEO::index_t>(c), first_vertex + static_cast<GEO::index_t>(facet[c]));
        }
        slots[f] = static_cast<GEO::index_t>(m_facet_slots[i]);
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
        }
    }
}

} // namespace editor
