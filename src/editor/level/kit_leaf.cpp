#include "level/kit_leaf.hpp"
#include "level/kit_mesh_builder.hpp"

#include "erhe_geometry/shapes/sweep.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>

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

// A thin wedge from a to b with its ridge raised (+y) by the thickness, counter-clockwise: a leaflet row of a folded leaf.
void make_wedge(const glm::vec2 a, const glm::vec2 b, const float thickness, std::vector<glm::vec2>& out_profile)
{
    const glm::vec2 d      = b - a;
    glm::vec2       normal = glm::normalize(glm::vec2{-d.y, d.x});
    if (normal.y < 0.0f) {
        normal = -normal;
    }
    const glm::vec2 ridge = (0.5f * (a + b)) + (thickness * normal);
    const float     area  = (d.x * (ridge.y - a.y)) - (d.y * (ridge.x - a.x));
    out_profile = (area > 0.0f) ? std::vector<glm::vec2>{a, b, ridge} : std::vector<glm::vec2>{a, ridge, b};
}

void add_sweep(Kit_mesh_builder& builder, const Kit_leaf& leaf, std::vector<glm::vec2>&& profile, const float tip_scale, GEO::Mesh& scratch)
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
            add_sweep(builder, leaf, std::move(profile), 0.0f, scratch);
            break;
        }
        case Kit_leaf_section::thick: {
            make_lens(glm::vec2{-half, 0.0f}, glm::vec2{half, 0.0f}, std::max(leaf.thickness, 0.33f * leaf.width), profile);
            add_sweep(builder, leaf, std::move(profile), 0.0f, scratch);
            break;
        }
        case Kit_leaf_section::folded: {
            // The halves overlap at the midrib and end in small caps, so no two vertices of the leaf share a position.
            const float overlap = 0.03f * leaf.width;
            const float drop    = leaf.fold * half;
            make_wedge(glm::vec2{-overlap, 0.0f}, glm::vec2{half, -drop}, leaf.thickness, profile);
            add_sweep(builder, leaf, std::move(profile), 0.04f, scratch);
            make_wedge(glm::vec2{overlap, 0.0f}, glm::vec2{-half, -drop}, leaf.thickness, profile);
            add_sweep(builder, leaf, std::move(profile), 0.04f, scratch);
            break;
        }
        default: {
            break;
        }
    }
}

} // namespace editor
