#include "level/kit_leaf.hpp"
#include "level/kit_mesh_builder.hpp"

#include "erhe_geometry/shapes/sweep.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

void add_sweep(Kit_mesh_builder& builder, const Kit_leaf& leaf, std::vector<glm::vec2>&& profile, const float tip_scale, const Base_cap base_cap, GEO::Mesh& scratch)
{
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
    add_kit_sweep(builder, parameters, scratch);
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
    switch (leaf.section) {
        case Kit_leaf_section::flat: {
            make_lens(glm::vec2{-half, 0.0f}, glm::vec2{half, 0.0f}, leaf.thickness, profile);
            add_sweep(builder, leaf, std::move(profile), 0.0f, Base_cap::sweep, scratch);
            break;
        }
        case Kit_leaf_section::thick: {
            make_lens(glm::vec2{-half, 0.0f}, glm::vec2{half, 0.0f}, std::max(leaf.thickness, 0.33f * leaf.width), profile);
            add_sweep(builder, leaf, std::move(profile), 0.0f, Base_cap::sweep, scratch);
            break;
        }
        case Kit_leaf_section::folded: {
            // One solid: the halves share the midrib's keel and top, and the tip is one vertex.
            const float     drop       = leaf.fold * half;
            const glm::vec2 left_edge {-half, -drop};
            const glm::vec2 right_edge{half, -drop};
            const glm::vec2 keel      {0.0f, 0.0f};
            const glm::vec2 top       {0.0f, leaf.thickness};
            profile = {left_edge, keel, right_edge, get_ridge(keel, right_edge, leaf.thickness), top, get_ridge(keel, left_edge, leaf.thickness)};
            const uint32_t base = static_cast<uint32_t>(builder.get_positions().size());
            add_sweep(builder, leaf, std::move(profile), 0.0f, Base_cap::none, scratch);
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
            add_sweep(builder, leaf, std::move(profile), 0.04f, Base_cap::sweep, scratch);
            make_wedge(glm::vec2{overlap, 0.0f}, glm::vec2{-half, -drop}, leaf.thickness, profile);
            add_sweep(builder, leaf, std::move(profile), 0.04f, Base_cap::sweep, scratch);
            break;
        }
        default: {
            break;
        }
    }
}

} // namespace editor
