#pragma once

#include <glm/glm.hpp>

#include <box3d/box3d.h>

#include <vector>

namespace erhe::physics {

// Box3D has only sphere, capsule, hull, mesh, height field and (static-only)
// baked compound shapes. Every other erhe primitive -- box, cylinder, tapered
// cylinder, tapered capsule, convex hull -- is represented as a convex hull.
//
// All hulls produced here are built in Box3D's canonical orientation: the axis
// of revolution is +Y and the shape is centered on the origin. erhe's Axis
// argument is folded into the shape's local transform when the shape is
// attached to a body, so a hull never has to be rebuilt for a different axis.

// Tessellation used for generated hulls.
//
// The binding Box3D limit is edges, not vertices: b3CreateHull rejects a hull
// whose half-edge count exceeds 2 * B3_MAX_HULL_EDGES (256), well before the
// B3_MAX_HULL_VERTICES (128) vertex limit bites. For a UV sphere with L
// longitudes and A latitude rings the counts are exactly
//   V = L * A + 2,  F = L * (A + 1),  E = V + F - 2 = L * (2A + 1)
// so the constraint is L * (2A + 1) <= 128. Exceeding it makes b3CreateHull
// return NULL, which surfaces as a logged error and an unusable shape -- so
// these constants are chosen with margin, and the unit tests assert that every
// generated hull is valid.
//
// The latitude counts must stay ODD so that one ring lands exactly on the
// equator. With an even count the widest sampled ring sits off the equator and
// the hull comes out measurably too thin (5% at 4 rings).

constexpr int hull_slice_count = 24; // radial slices of cylinders / cones; b3 allows [3, 32]

// Standalone sphere: 16 * 7 = 112 edges.
constexpr int hull_sphere_longitude = 16;
constexpr int hull_sphere_latitude  = 3; // keep odd

// Capsule caps. Two spheres share one hull, so each cap gets a smaller budget;
// the hemispheres facing each other are swallowed by the hull's side surface,
// which is why this fits at all.
constexpr int hull_capsule_longitude = 12;
constexpr int hull_capsule_latitude  = 3; // keep odd

// Owns a b3HullData allocated by Box3D. Hulls are built once per descriptor and
// shared by every body that uses that shape; b3CreateTransformedHullShape takes
// a transform and a scale, so a hull is never cloned per body.
class Box3d_hull
{
public:
    Box3d_hull() = default;
    explicit Box3d_hull(b3HullData* hull);
    ~Box3d_hull() noexcept;

    Box3d_hull           (const Box3d_hull&) = delete;
    Box3d_hull& operator=(const Box3d_hull&) = delete;
    Box3d_hull           (Box3d_hull&& other) noexcept;
    Box3d_hull& operator=(Box3d_hull&& other) noexcept;

    [[nodiscard]] auto get     () const -> const b3HullData* { return m_hull; }
    [[nodiscard]] auto is_valid() const -> bool              { return m_hull != nullptr; }

private:
    b3HullData* m_hull{nullptr};
};

// Point generators. Each clears the output vector before filling it, so a
// caller can reuse one buffer across shapes without reallocating.
void make_box_hull_points(const glm::vec3& half_extents, std::vector<b3Vec3>& points);

// Centered on the origin, axis of revolution +Y, spanning [-half_height, half_height].
// A zero radius collapses that end to a single apex point, which is how a cone
// is expressed (erhe has no cone shape type; it uses a tapered cylinder with one
// radius at ~0). Box3D's own b3CreateCone asserts both radii are positive and
// places the base at y = 0, so it is not used here.
void make_tapered_cylinder_hull_points(float bottom_radius, float top_radius, float half_height, std::vector<b3Vec3>& points);

// Appends a tessellated sphere; does NOT clear the output vector.
void append_sphere_hull_points(const glm::vec3& center, float radius, int longitude_count, int latitude_count, std::vector<b3Vec3>& points);

// A sphero-cone is by definition the convex hull of its two end spheres, so
// tessellating both spheres converges on the true shape. half_length is half the
// distance between the cap centers, matching erhe's capsule length convention.
void make_tapered_capsule_hull_points(float bottom_radius, float top_radius, float half_length, std::vector<b3Vec3>& points);

// Hull factories. Each returns an invalid Box3d_hull and logs an error when the
// input is degenerate (b3CreateHull returns NULL for fewer than 4 points or a
// point set with no volume).
[[nodiscard]] auto create_box_hull             (const glm::vec3& half_extents) -> Box3d_hull;
[[nodiscard]] auto create_cylinder_hull        (float radius, float half_height) -> Box3d_hull;
[[nodiscard]] auto create_tapered_cylinder_hull(float bottom_radius, float top_radius, float half_height) -> Box3d_hull;
[[nodiscard]] auto create_sphere_hull          (float radius) -> Box3d_hull;
[[nodiscard]] auto create_capsule_hull         (float radius, float half_length) -> Box3d_hull;
[[nodiscard]] auto create_tapered_capsule_hull (float bottom_radius, float top_radius, float half_length) -> Box3d_hull;

// points is a packed array of point_count positions, each stride bytes apart.
[[nodiscard]] auto create_convex_hull(const float* points, int point_count, int stride) -> Box3d_hull;

[[nodiscard]] auto create_hull_from_points(const std::vector<b3Vec3>& points, const char* debug_label) -> Box3d_hull;

} // namespace erhe::physics
