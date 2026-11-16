#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace editor {

namespace {

// A box on y = 0 around the Y axis; corner k has x at bit 0, y at bit 1 and z at bit 2.
void add_base_box_v2(Kit_mesh_builder& builder, const float half_x, const float height, const float half_z)
{
    std::array<uint32_t, 8> v{};
    for (uint32_t k = 0; k < 8; ++k) {
        v[k] = builder.add_vertex(glm::vec3{
            ((k & 1u) != 0u) ? half_x : -half_x,
            ((k & 2u) != 0u) ? height : 0.0f,
            ((k & 4u) != 0u) ? half_z : -half_z
        });
    }
    builder.add_quad(v[2], v[6], v[7], v[3]); // +y
    builder.add_quad(v[0], v[1], v[5], v[4]); // -y
    builder.add_quad(v[4], v[5], v[7], v[6]); // +z
    builder.add_quad(v[0], v[2], v[3], v[1]); // -z
    builder.add_quad(v[1], v[3], v[7], v[5]); // +x
    builder.add_quad(v[0], v[4], v[6], v[2]); // -x
}

void generate_block_v2(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    Kit_mesh_builder builder;
    add_base_box_v2(builder, 0.5f * values.get_float("width"), values.get_float("height"), 0.5f * values.get_float("depth"));
    builder.write(mesh);
}

void generate_floor_slab_v2(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    Kit_mesh_builder builder;
    add_base_box_v2(builder, 0.5f * values.get_float("width"), values.get_float("thickness"), 0.5f * values.get_float("depth"));
    builder.write(mesh);
}

void generate_wedge_v2(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float half_run = 0.5f * values.get_float("run");
    const float rise     = values.get_float("rise");
    const float half_x   = 0.5f * values.get_float("width");
    Kit_mesh_builder builder;
    const uint32_t foot_left  = builder.add_vertex(glm::vec3{-half_x, 0.0f,  half_run});
    const uint32_t foot_right = builder.add_vertex(glm::vec3{ half_x, 0.0f,  half_run});
    const uint32_t back_left  = builder.add_vertex(glm::vec3{-half_x, 0.0f, -half_run});
    const uint32_t back_right = builder.add_vertex(glm::vec3{ half_x, 0.0f, -half_run});
    const uint32_t top_left   = builder.add_vertex(glm::vec3{-half_x, rise, -half_run});
    const uint32_t top_right  = builder.add_vertex(glm::vec3{ half_x, rise, -half_run});
    builder.add_quad    (foot_left, foot_right, top_right, top_left);   // slope
    builder.add_quad    (foot_left, back_left, back_right, foot_right); // -y
    builder.add_quad    (back_left, top_left, top_right, back_right);   // -z
    builder.add_triangle(foot_left, top_left, back_left);               // -x
    builder.add_triangle(foot_right, back_right, top_right);            // +x
    builder.write(mesh);
}

// Point i of n on the unit circle, (x, z) from +X toward -Z; mirror images are equal to the bit and axis points exact.
auto get_circle_point_v2(const int i, const int n) -> glm::vec2
{
    if ((2 * i) > n) {
        const glm::vec2 mirrored = get_circle_point_v2(n - i, n);
        return glm::vec2{mirrored.x, -mirrored.y};
    }
    if (((n % 2) == 0) && ((4 * i) > n)) {
        const glm::vec2 mirrored = get_circle_point_v2((n / 2) - i, n);
        return glm::vec2{-mirrored.x, mirrored.y};
    }
    if (i == 0) {
        return glm::vec2{1.0f, 0.0f};
    }
    if ((4 * i) == n) {
        return glm::vec2{0.0f, -1.0f};
    }
    const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(n);
    return glm::vec2{std::cos(angle), -std::sin(angle)};
}

void generate_cylinder_v2(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const int   segments = values.get_int("segments");
    const float radius   = values.get_float("radius");
    const float height   = values.get_float("height");
    Kit_mesh_builder builder;
    // Each side edge is one (x, z) used at both ends, so the sides are exactly vertical.
    std::vector<uint32_t> bottom_ring;
    std::vector<uint32_t> top_ring;
    for (int i = 0; i < segments; ++i) {
        const glm::vec2 point = radius * get_circle_point_v2(i, segments);
        bottom_ring.push_back(builder.add_vertex(glm::vec3{point.x, 0.0f,   point.y}));
        top_ring   .push_back(builder.add_vertex(glm::vec3{point.x, height, point.y}));
    }
    const std::size_t count = bottom_ring.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t next = (i + 1) % count;
        builder.add_quad(bottom_ring[i], bottom_ring[next], top_ring[next], top_ring[i]);
    }
    // One planar polygon per cap: a hard rim and no center vertex.
    builder.add_polygon(top_ring);
    const std::vector<uint32_t> bottom_cap(bottom_ring.rbegin(), bottom_ring.rend());
    builder.add_polygon(bottom_cap);
    builder.write(mesh);
}

// The vertices of one side of the stairs: index i is at the front edge of step i (i = step count: the back).
class Stairs_side_v2
{
public:
    std::vector<uint32_t> base;  // on y = 0
    std::vector<uint32_t> inner; // the foot of riser i, the back end of tread i - 1; inner[0] is base[0]
    std::vector<uint32_t> nose;  // the top of riser i, the front end of tread i
};

enum class Stairs_side_facing_v2 : unsigned int { minus_x = 0, plus_x };

// The side wall: a column per step, a trapezoid up to the line of the riser feet and the step's triangle above it.
void add_stairs_side_facets_v2(Kit_mesh_builder& builder, const Stairs_side_v2& side, const Stairs_side_facing_v2 facing)
{
    const bool        plus_x = (facing == Stairs_side_facing_v2::plus_x);
    const std::size_t steps  = side.nose.size();
    for (std::size_t i = 0; i < steps; ++i) {
        // Counter-clockwise seen from +X: the front is on the left.
        const uint32_t upper_front = (i == 0) ? side.nose[0] : side.inner[i];
        if (plus_x) {
            builder.add_quad(side.base[i], side.base[i + 1], side.inner[i + 1], upper_front);
        } else {
            builder.add_quad(side.base[i], upper_front, side.inner[i + 1], side.base[i + 1]);
        }
        if (i == 0) {
            continue;
        }
        if (plus_x) {
            builder.add_triangle(side.inner[i], side.inner[i + 1], side.nose[i]);
        } else {
            builder.add_triangle(side.inner[i], side.nose[i], side.inner[i + 1]);
        }
    }
}

void generate_stairs_v2(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Kit_stairs_layout layout = get_stairs_layout(values);
    const int   steps    = layout.step_count;
    const float rise     = values.get_float("rise");
    const float half_x   = 0.5f * values.get_float("width");
    const float half_run = 0.5f * layout.run;

    Kit_mesh_builder builder;
    std::array<Stairs_side_v2, 2> sides; // -x, +x
    for (std::size_t s = 0; s < 2; ++s) {
        const float     x    = (s == 0) ? -half_x : half_x;
        Stairs_side_v2& side = sides[s];
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float z = half_run - (layout.run * t);
            const float y = rise * t;
            side.base.push_back(builder.add_vertex(glm::vec3{x, 0.0f, z}));
            side.inner.push_back((i == 0) ? side.base[0] : builder.add_vertex(glm::vec3{x, y, z}));
            if (i < steps) {
                const float tread_y = rise * (static_cast<float>(i + 1) / static_cast<float>(steps));
                side.nose.push_back(builder.add_vertex(glm::vec3{x, tread_y, z}));
            }
        }
    }
    const Stairs_side_v2& left  = sides[0];
    const Stairs_side_v2& right = sides[1];
    const std::size_t     count = static_cast<std::size_t>(steps);
    for (std::size_t i = 0; i < count; ++i) {
        builder.add_quad(left.inner[i], right.inner[i], right.nose[i], left.nose[i]);          // riser
        builder.add_quad(left.nose[i], right.nose[i], right.inner[i + 1], left.inner[i + 1]);  // tread
    }
    for (std::size_t i = 0; i < count; ++i) {
        builder.add_quad(left.base[i], left.base[i + 1], right.base[i + 1], right.base[i]);    // -y, a strip per step
    }
    builder.add_quad(left.base[count], left.inner[count], right.inner[count], right.base[count]); // -z
    add_stairs_side_facets_v2(builder, left,  Stairs_side_facing_v2::minus_x);
    add_stairs_side_facets_v2(builder, right, Stairs_side_facing_v2::plus_x);
    builder.write(mesh);
}

} // anonymous namespace

void add_basic_shapes_v2(std::vector<Kit_shape_definition>& shapes)
{
    shapes.push_back(make_recreated_shape(shapes, "basic.block",      generate_block_v2,      12));
    shapes.push_back(make_recreated_shape(shapes, "basic.wedge",      generate_wedge_v2,      8));
    shapes.push_back(make_recreated_shape(shapes, "basic.cylinder",   generate_cylinder_v2,   60));
    shapes.push_back(make_recreated_shape(shapes, "basic.stairs",     generate_stairs_v2,     180));
    shapes.push_back(make_recreated_shape(shapes, "basic.floor_slab", generate_floor_slab_v2, 12));
}

} // namespace editor
