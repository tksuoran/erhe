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

enum class Kit_blob_cap : unsigned int {
    flat = 0, // planar fan from a center vertex at the end ring's height
    point     // single apex vertex on the axis
};

// One ring of a blob: height and radius before jitter.
class Kit_blob_ring
{
public:
    float y     {0.0f};
    float radius{0.0f};
};

// A blob is a star-shaped solid around the Y axis: rings bottom to top, closed by caps.
class Kit_blob
{
public:
    std::vector<Kit_blob_ring> rings;
    int          segments          {8};
    Kit_blob_cap bottom_cap        {Kit_blob_cap::flat};
    Kit_blob_cap top_cap           {Kit_blob_cap::point};
    float        bottom_point_y    {0.0f};  // used when bottom_cap is point
    float        top_point_y       {0.0f};  // used when top_cap is point
    float        angle_jitter      {0.0f};  // radial factor shared by all rings at one angle, +- this
    float        vertex_jitter     {0.0f};  // radial factor per vertex, +- this
    float        height_jitter     {0.0f};  // meters per vertex, +- this; not applied on flat cap rings
    float        apex_offset       {0.0f};  // horizontal apex offset, +- this times the nearest ring radius
};

// Collects positions and convex counter-clockwise facets, then writes them into a GEO::Mesh.
class Kit_mesh_builder
{
public:
    auto add_vertex  (const glm::vec3& position) -> uint32_t;
    void add_triangle(uint32_t a, uint32_t b, uint32_t c);
    void add_quad    (uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void add_polygon (std::span<const uint32_t> corners);

    void add_box     (const glm::vec3& min_corner, const glm::vec3& max_corner);
    // Regular prism around the Y axis; the first corner sits at phase_radians from +X toward -Z.
    void add_prism   (int segments, float radius, float y0, float y1, float phase_radians);
    void add_blob    (const Kit_blob& blob, Kit_random& random);

    void write       (GEO::Mesh& mesh) const;

    [[nodiscard]] auto get_positions() const -> const std::vector<glm::vec3>&;

private:
    std::vector<glm::vec3>             m_positions;
    std::vector<std::vector<uint32_t>> m_facets;
};

} // namespace editor
