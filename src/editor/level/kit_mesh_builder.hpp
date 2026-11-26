#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace GEO { class Mesh; }

namespace editor {

// Random stream of a seeded shape: splitmix64, integer arithmetic only, so every platform draws the same numbers (D1.4).
class Kit_random
{
public:
    explicit Kit_random(int seed);
    // A stream of its own per name: the seed mixed with a hash of the name (D1.8).
    Kit_random(int seed, std::string_view stream_name);

    [[nodiscard]] auto next_u32() -> uint32_t;
    [[nodiscard]] auto next_unit() -> float;                     // [0, 1)
    [[nodiscard]] auto next_range(float min_value, float max_value) -> float;

private:
    uint64_t m_state{0};
};

// The facet attribute of a generated mesh that names each facet's material slot, an index into
// Kit_shape_definition::material_slots (D1.11).
constexpr const char* c_kit_slot_attribute = "kit_slot";

// Collects positions and convex counter-clockwise facets, then writes them into a GEO::Mesh.
class Kit_mesh_builder
{
public:
    // The material slot of the facets added after this call; 0 until set.
    void set_slot    (uint32_t slot);

    auto add_vertex  (const glm::vec3& position) -> uint32_t;
    void add_triangle(uint32_t a, uint32_t b, uint32_t c);
    void add_quad    (uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void add_polygon (std::span<const uint32_t> corners);

    void add_box     (const glm::vec3& min_corner, const glm::vec3& max_corner);
    // Regular prism around the Y axis; the first corner sits at phase_radians from +X toward -Z.
    void add_prism   (int segments, float radius, float y0, float y1, float phase_radians);
    // A solid of rings bottom to top, each of the segment count, its points counter-clockwise seen from above, closed by
    // fans to the cap points (a flat cap's point lies in its ring's plane); two triangles per quad.
    void add_ring_solid(std::span<const glm::vec3> ring_points, int segments, const glm::vec3& bottom_point, const glm::vec3& top_point);
    // As add_ring_solid(), the caps strips of quads pairing point i with point (segments - 1 - i) across the first and
    // the last ring (an even count, each ring symmetric about x = 0 in z): a long narrow solid, where fans make slivers.
    void add_ring_solid_paired_caps(std::span<const glm::vec3> ring_points, int segments);
    // A prism along X from x0 to x1 over a section of (z, y) points, counter-clockwise seen from +X; each end cap is the
    // convex pieces of the section (indices into it), so a concave section closes with the pieces sharing their edges.
    void add_x_prism (float x0, float x1, std::span<const glm::vec2> section, std::span<const std::vector<uint32_t>> cap_pieces);
    // The facets of a generated mesh (a sweep) under the current slot; a quad becomes two triangles, since a bent sweep's quads are not planar.
    void add_mesh    (const GEO::Mesh& mesh);
    // A box from a to b (P4), a != b: width along the horizontal normal of the axis, height along the normal above it.
    void add_beam    (const glm::vec3& a, const glm::vec3& b, float width, float height);
    // Triangles between two polylines whose keys rise along each, advancing on the smaller next key; they wind
    // (first[i], first[i + 1], second[j]). A closed ring repeats its first vertex at the end with its key plus a turn.
    void add_band    (std::span<const uint32_t> first, std::span<const float> first_keys, std::span<const uint32_t> second, std::span<const float> second_keys);

    // A designed sharp edge between two vertices of a facet edge: the normals of the vertices on it do not average across it.
    void add_hard_edge(uint32_t a, uint32_t b);

    // Writes the facets and their slots (c_kit_slot_attribute), and the corner normals of every corner at a vertex of a
    // hard edge (doc/editor/level_kit_shapes.md "Hard edges").
    void write       (GEO::Mesh& mesh) const;

    [[nodiscard]] auto get_positions() const -> const std::vector<glm::vec3>&;

private:
    std::vector<glm::vec3>             m_positions;
    std::vector<std::vector<uint32_t>> m_facets;
    std::vector<uint32_t>              m_facet_slots;
    std::vector<uint64_t>              m_hard_edges; // (min vertex << 32) | max vertex
    uint32_t                           m_slot{0};
};

// The angle of a polygon at its corner, radians; 0 for a corner with a zero-length side.
[[nodiscard]] auto get_kit_corner_angle(const glm::vec3& previous, const glm::vec3& corner, const glm::vec3& next) -> float;

// The slot of every facet of a generated mesh; 0 for every facet of a mesh without c_kit_slot_attribute.
void read_kit_facet_slots(const GEO::Mesh& mesh, std::vector<uint32_t>& out_slots);

// The facets of one slot with the vertices they use, into an empty mesh.
void extract_kit_slot_mesh(const GEO::Mesh& source, std::span<const uint32_t> facet_slots, uint32_t slot, GEO::Mesh& out_mesh);

} // namespace editor
