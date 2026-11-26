#include "level/kit_leaf.hpp"
#include "level/kit_mesh_builder.hpp"

#include "erhe_geometry/shapes/sweep.hpp"

#include <geogram/mesh/mesh.h>

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace editor {

namespace {

// A thin lens from a to b in the section plane, counter-clockwise (outward walls).
void make_lens(const glm::vec2 a, const glm::vec2 b, const float thickness, std::vector<glm::vec2>& out_profile)
{
    const glm::vec2 d      = b - a;
    const glm::vec2 normal = glm::normalize(glm::vec2{-d.y, d.x});
    const glm::vec2 middle = 0.5f * (a + b);
    out_profile = {a, middle - (0.5f * thickness * normal), b, middle + (0.5f * thickness * normal)};
}

// The middle of the line from a to b raised (+y) by the thickness: the ridge of a folded leaf's half.
[[nodiscard]] auto get_ridge(const glm::vec2 a, const glm::vec2 b, const float thickness) -> glm::vec2
{
    const glm::vec2 d      = b - a;
    glm::vec2       normal = glm::normalize(glm::vec2{-d.y, d.x});
    if (normal.y < 0.0f) {
        normal = -normal;
    }
    return (0.5f * (a + b)) + (thickness * normal);
}

// A thin wedge from a to b with its ridge raised (+y) by the thickness, counter-clockwise: a leaflet row of a folded_split leaf.
void make_wedge(const glm::vec2 a, const glm::vec2 b, const float thickness, std::vector<glm::vec2>& out_profile)
{
    const glm::vec2 d     = b - a;
    const glm::vec2 ridge = get_ridge(a, b, thickness);
    const float     area  = (d.x * (ridge.y - a.y)) - (d.y * (ridge.x - a.x));
    out_profile = (area > 0.0f) ? std::vector<glm::vec2>{a, b, ridge} : std::vector<glm::vec2>{a, ridge, b};
}

enum class Base_cap : unsigned int {
    sweep = 0, // the sweep's own start cap: a convex profile
    none       // the caller caps the base ring
};

// The hard edges of a leaf swept from base_vertex with its tip collapsed to one vertex: along each line (a profile
// index) from the base to the tip, and around the base ring, where the cap meets the walls.
void add_leaf_hard_edges(Kit_mesh_builder& builder, const uint32_t base_vertex, const uint32_t profile_count, const uint32_t ring_count, const std::span<const uint32_t> lines)
{
    const auto ring_vertex = [base_vertex, profile_count](const uint32_t ring, const uint32_t j) -> uint32_t {
        return base_vertex + (ring * profile_count) + j;
    };
    const uint32_t tip = base_vertex + (ring_count * profile_count);
    for (const uint32_t j : lines) {
        for (uint32_t ring = 0; (ring + 1) < ring_count; ++ring) {
            builder.add_hard_edge(ring_vertex(ring, j), ring_vertex(ring + 1, j));
        }
        builder.add_hard_edge(ring_vertex(ring_count - 1, j), tip);
    }
    for (uint32_t j = 0; j < profile_count; ++j) {
        builder.add_hard_edge(ring_vertex(0, j), ring_vertex(0, (j + 1) % profile_count));
    }
}

// lines: the profile indices of the leaf's hard edges (add_leaf_hard_edges()), for a tip_scale of 0.
void add_sweep(
    Kit_mesh_builder&              builder,
    const Kit_leaf&                leaf,
    std::vector<glm::vec2>&&       profile,
    const float                    tip_scale,
    const Base_cap                 base_cap,
    const std::span<const uint32_t> lines,
    GEO::Mesh&                     scratch
)
{
    const uint32_t base_vertex   = static_cast<uint32_t>(builder.get_positions().size());
    const uint32_t profile_count = static_cast<uint32_t>(profile.size());
    if (leaf.roll != 0.0f) {
        const float c = std::cos(leaf.roll);
        const float s = std::sin(leaf.roll);
        for (glm::vec2& p : profile) {
            p = glm::vec2{(c * p.x) - (s * p.y), (s * p.x) + (c * p.y)};
        }
    }
    erhe::geometry::shapes::Sweep_parameters parameters;
    parameters.profile     = std::move(profile);
    parameters.spine       = std::vector<glm::vec3>{leaf.spine.begin(), leaf.spine.end()};
    parameters.spine_steps = std::max(1, leaf.steps);
    parameters.start_cap   = (base_cap == Base_cap::sweep);
    const float widest_at = std::clamp(leaf.widest_at, 0.05f, 0.95f);
    parameters.taper = {glm::vec2{0.0f, leaf.base_width}, glm::vec2{widest_at, 1.0f}};
    if (leaf.widest_end > widest_at) {
        parameters.taper.emplace_back(std::min(leaf.widest_end, 0.95f), 1.0f);
    }
    parameters.taper.emplace_back(1.0f, tip_scale);
    const uint32_t ring_count = static_cast<uint32_t>(parameters.spine_steps); // the last station is the tip
    add_kit_sweep(builder, parameters, scratch);
    if (!lines.empty()) {
        ERHE_VERIFY(tip_scale == 0.0f);
        add_leaf_hard_edges(builder, base_vertex, profile_count, ring_count, lines);
    }
}

constexpr float folded_arch_power = 1.6f; // a folded half's underside drops by (share across) ^ this: the leaflet rows arch down

// A folded leaf whose halves are leaf.across segments each, along an underside dropping by (share across) ^ power to the
// edge: points of the underside from the left edge over the keel to the right edge, then the upper side back, each upper
// point over the middle of its underside segment, as thick as the leaf but thinning over the outer half to the edge.
void add_arched_folded_leaf(Kit_mesh_builder& builder, const Kit_leaf& leaf, GEO::Mesh& scratch)
{
    const int   k    = leaf.across;
    const float half = 0.5f * leaf.width;
    const float drop = leaf.fold * half;
    const auto  under = [half, drop](const float side, const float u) -> glm::vec2 {
        return glm::vec2{side * u * half, -drop * std::pow(u, folded_arch_power)};
    };
    const auto  upper = [&leaf, k, &under](const float side, const int i) -> glm::vec2 {
        const float     u0     = static_cast<float>(i) / static_cast<float>(k);
        const float     u1     = static_cast<float>(i + 1) / static_cast<float>(k);
        const glm::vec2 a      = under(side, u0);
        const glm::vec2 b      = under(side, u1);
        glm::vec2       normal = glm::normalize(glm::vec2{-(b - a).y, (b - a).x});
        if (normal.y < 0.0f) {
            normal = -normal;
        }
        const float middle = 0.5f * (u0 + u1);
        return (0.5f * (a + b)) + ((leaf.thickness * std::min(1.0f, 2.0f * (1.0f - middle))) * normal);
    };
    // The right upper points (the left mirror them); at an inner corner where the underside bends down, the two beside
    // it rise until the leaf is as thick over the corner as a segment's middle there would be.
    std::vector<glm::vec2> right_upper_points;
    for (int i = 0; i < k; ++i) {
        right_upper_points.push_back(upper(1.0f, i));
    }
    for (int j = 1; j < k; ++j) {
        const float     u        = static_cast<float>(j) / static_cast<float>(k);
        const glm::vec2 corner   = under(1.0f, u);
        glm::vec2&      inner    = right_upper_points[static_cast<std::size_t>(j - 1)];
        glm::vec2&      outer    = right_upper_points[static_cast<std::size_t>(j)];
        const float     share    = (corner.x - inner.x) / (outer.x - inner.x);
        const float     over     = (inner.y + (share * (outer.y - inner.y))) - corner.y;
        const float     required = leaf.thickness * std::min(1.0f, 2.0f * (1.0f - u));
        if (over < required) {
            inner.y += required - over;
            outer.y += required - over;
        }
    }
    // Profile indices: the left underside from the edge at 0, the keel at k, the right underside to 2k, the right
    // upper side from its outer point at 2k + 1 to its inner one at 3k, the keel's top at 3k + 1, the left upper side
    // from its inner point at 3k + 2 to its outer one at 4k + 1.
    std::vector<glm::vec2> profile;
    for (int j = k; j > 0; --j) {
        profile.push_back(under(-1.0f, static_cast<float>(j) / static_cast<float>(k)));
    }
    profile.push_back(glm::vec2{0.0f, 0.0f});
    for (int j = 1; j <= k; ++j) {
        profile.push_back(under(1.0f, static_cast<float>(j) / static_cast<float>(k)));
    }
    for (int i = k - 1; i >= 0; --i) {
        profile.push_back(right_upper_points[static_cast<std::size_t>(i)]);
    }
    profile.push_back(glm::vec2{0.0f, leaf.thickness});
    for (int i = 0; i < k; ++i) {
        const glm::vec2 point = right_upper_points[static_cast<std::size_t>(i)];
        profile.push_back(glm::vec2{-point.x, point.y});
    }
    // The hard lines: the outer edges of both sides, the keel and its top.
    const uint32_t ku_lines = static_cast<uint32_t>(k);
    const std::array<uint32_t, 6> lines{0, ku_lines, 2 * ku_lines, (2 * ku_lines) + 1, (3 * ku_lines) + 1, (4 * ku_lines) + 1};
    const uint32_t base = static_cast<uint32_t>(builder.get_positions().size());
    add_sweep(builder, leaf, std::move(profile), 0.0f, Base_cap::none, lines, scratch);

    // The base cap in triangles sharing their edges, wound to face back along the spine (the profile's
    // counter-clockwise triangles reversed).
    const uint32_t ku       = static_cast<uint32_t>(k);
    const uint32_t keel     = base + ku;
    const uint32_t keel_top = base + (3 * ku) + 1;
    const auto right_under = [base, ku](const uint32_t j) -> uint32_t { return base + ku + j; };
    const auto left_under  = [base, ku](const uint32_t j) -> uint32_t { return base + ku - j; };
    const auto right_upper = [base, ku](const uint32_t i) -> uint32_t { return base + (2 * ku) + 1 + (ku - 1 - i); };
    const auto left_upper  = [base, ku](const uint32_t i) -> uint32_t { return base + (3 * ku) + 2 + i; };
    const auto add_cap_triangle = [&builder](const uint32_t a, const uint32_t b, const uint32_t c) {
        builder.add_triangle(c, b, a);
    };
    add_cap_triangle(keel, right_upper(0), keel_top);
    add_cap_triangle(keel, keel_top, left_upper(0));
    for (uint32_t i = 0; i < ku; ++i) {
        add_cap_triangle(right_under(i), right_under(i + 1), right_upper(i));
        add_cap_triangle(left_under(i + 1), left_under(i), left_upper(i));
        if ((i + 1) < ku) {
            add_cap_triangle(right_under(i + 1), right_upper(i + 1), right_upper(i));
            add_cap_triangle(left_under(i + 1), left_upper(i), left_upper(i + 1));
        }
    }
}

} // anonymous namespace

void add_kit_sweep(Kit_mesh_builder& builder, const erhe::geometry::shapes::Sweep_parameters& parameters, GEO::Mesh& scratch)
{
    scratch.clear(true, true);
    erhe::geometry::shapes::make_sweep(scratch, parameters);
    builder.add_mesh(scratch);
}

void add_leaf(Kit_mesh_builder& builder, const Kit_leaf& leaf, GEO::Mesh& scratch)
{
    const float half = 0.5f * leaf.width;
    std::vector<glm::vec2> profile;
    constexpr std::array<uint32_t, 2> lens_lines{0, 2};      // the lens's two edges
    constexpr std::array<uint32_t, 4> folded_lines{0, 1, 2, 4}; // the outer edges, the keel and its top
    switch (leaf.section) {
        case Kit_leaf_section::flat: {
            make_lens(glm::vec2{-half, 0.0f}, glm::vec2{half, 0.0f}, leaf.thickness, profile);
            add_sweep(builder, leaf, std::move(profile), 0.0f, Base_cap::sweep, lens_lines, scratch);
            break;
        }
        case Kit_leaf_section::thick: {
            make_lens(glm::vec2{-half, 0.0f}, glm::vec2{half, 0.0f}, std::max(leaf.thickness, 0.33f * leaf.width), profile);
            add_sweep(builder, leaf, std::move(profile), 0.0f, Base_cap::sweep, lens_lines, scratch);
            break;
        }
        case Kit_leaf_section::folded: {
            if (leaf.across > 1) {
                add_arched_folded_leaf(builder, leaf, scratch);
                break;
            }
            // One solid: the halves share the midrib's keel and top, and the tip is one vertex.
            const float     drop       = leaf.fold * half;
            const glm::vec2 left_edge {-half, -drop};
            const glm::vec2 right_edge{half, -drop};
            const glm::vec2 keel      {0.0f, 0.0f};
            const glm::vec2 top       {0.0f, leaf.thickness};
            profile = {left_edge, keel, right_edge, get_ridge(keel, right_edge, leaf.thickness), top, get_ridge(keel, left_edge, leaf.thickness)};
            const uint32_t base = static_cast<uint32_t>(builder.get_positions().size());
            add_sweep(builder, leaf, std::move(profile), 0.0f, Base_cap::none, folded_lines, scratch);
            // The V profile is concave: its base cap is one convex quad per half, the two sharing the midrib.
            builder.add_quad(base + 5, base + 4, base + 1, base + 0);
            builder.add_quad(base + 4, base + 3, base + 2, base + 1);
            break;
        }
        case Kit_leaf_section::folded_split: {
            // The halves overlap at the midrib and end in small caps, so no two vertices of the leaf share a position.
            const float overlap = 0.03f * leaf.width;
            const float drop    = leaf.fold * half;
            make_wedge(glm::vec2{-overlap, 0.0f}, glm::vec2{half, -drop}, leaf.thickness, profile);
            add_sweep(builder, leaf, std::move(profile), 0.04f, Base_cap::sweep, {}, scratch);
            make_wedge(glm::vec2{overlap, 0.0f}, glm::vec2{-half, -drop}, leaf.thickness, profile);
            add_sweep(builder, leaf, std::move(profile), 0.04f, Base_cap::sweep, {}, scratch);
            break;
        }
        default: {
            break;
        }
    }
}

} // namespace editor
