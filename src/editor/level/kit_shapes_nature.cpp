#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/conway/chamfer3.hpp"
#include "erhe_geometry/shapes/convex_hull.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

namespace editor {

namespace {

constexpr int      tree_trunk_segments = 6;
constexpr uint32_t tree_bark_slot      = 0; // the trunk
constexpr uint32_t tree_foliage_slot   = 1; // the canopy
constexpr float round_canopy_height_per_size   = 0.9f;
constexpr float conical_canopy_height_per_size = 1.4f;

constexpr int rock_blob_segments = 7;

// Bevel ratio of the first and the second bevel pass (MCP chamfer, doc/erhe/geometry.md "Conway operators").
constexpr std::array<float, 2> rock_bevel_ratios{0.22f, 0.12f};

// A hull form of the rock (D1.9): the point cloud and where the base is cut.
class Rock_hull_recipe
{
public:
    int       point_count;          // with zero or one bevel pass
    int       beveled_twice_points; // with two passes, which keep the mesh within the kit's ceiling
    glm::vec3 half_extent;          // per meter of size
    float     extent_jitter;        // per axis, +- this share
    float     base_cut;             // the base plane, this share of the beveled mesh's height above its lowest point
};

[[nodiscard]] auto get_rock_hull_recipe(const std::string_view form) -> Rock_hull_recipe
{
    if (form == "worn") {
        return Rock_hull_recipe{22, 10, glm::vec3{0.5f, 0.40f, 0.5f}, 0.12f, 0.22f};
    }
    if (form == "slab") {
        return Rock_hull_recipe{12, 9, glm::vec3{0.5f, 0.18f, 0.42f}, 0.12f, 0.25f};
    }
    return Rock_hull_recipe{10, 9, glm::vec3{0.5f, 0.42f, 0.5f}, 0.2f, 0.22f}; // angular
}

void generate_blob_rock(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float size      = values.get_float("size");
    const float roughness = values.get_float("roughness");
    const float r         = 0.5f * size;
    const float h         = 0.75f * size;
    Kit_random random{values.get_seed()};
    Kit_blob   blob;
    blob.rings         = {{0.0f, 0.7f * r}, {0.35f * h, r}, {0.75f * h, 0.75f * r}};
    blob.segments      = rock_blob_segments;
    blob.bottom_cap    = Kit_blob_cap::flat;
    blob.top_cap       = Kit_blob_cap::point;
    blob.top_point_y   = h;
    blob.angle_jitter  = 0.25f * roughness;
    blob.vertex_jitter = 0.1f * roughness;
    blob.height_jitter = 0.08f * h * roughness;
    blob.apex_offset   = 0.2f * roughness;
    Kit_mesh_builder builder;
    builder.add_blob(blob, random);
    builder.write(mesh);
}

// Points of a jittered Fibonacci sphere scaled to the half extents.
void make_rock_points(const Rock_hull_recipe& recipe, const int point_count, const float size, const float roughness, Kit_random& random, std::vector<glm::vec3>& out_points)
{
    const glm::vec3 half{
        recipe.half_extent.x * size * (1.0f + random.next_range(-recipe.extent_jitter, recipe.extent_jitter)),
        recipe.half_extent.y * size * (1.0f + random.next_range(-recipe.extent_jitter, recipe.extent_jitter)),
        recipe.half_extent.z * size * (1.0f + random.next_range(-recipe.extent_jitter, recipe.extent_jitter))
    };
    const float golden_angle = std::numbers::pi_v<float> * (3.0f - std::sqrt(5.0f));
    const float phase        = random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    // About the spacing of neighbouring points at roughness 1.
    const float spread       = 1.8f / std::sqrt(static_cast<float>(point_count));
    out_points.clear();
    for (int i = 0; i < point_count; ++i) {
        const float y     = 1.0f - ((2.0f * (static_cast<float>(i) + 0.5f)) / static_cast<float>(point_count));
        const float r     = std::sqrt(std::max(0.0f, 1.0f - (y * y)));
        const float angle = phase + (golden_angle * static_cast<float>(i));
        glm::vec3 direction{r * std::cos(angle), y, r * std::sin(angle)};
        direction += (roughness * spread) * glm::vec3{random.next_range(-1.0f, 1.0f), random.next_range(-1.0f, 1.0f), random.next_range(-1.0f, 1.0f)};
        const float radius = 1.0f + (roughness * random.next_range(-0.4f, 0.4f));
        out_points.push_back(glm::normalize(direction) * radius * half);
    }
}

void add_welded_point(std::vector<glm::vec3>& points, const glm::vec3& point, const float weld_distance)
{
    for (const glm::vec3& existing : points) {
        if (glm::dot(existing - point, existing - point) < (weld_distance * weld_distance)) {
            return;
        }
    }
    points.push_back(point);
}

// The convex mesh cut by the plane y = cut_y: its vertices above the plane and its edges' crossings, hulled again.
// The base is one polygon at y = 0 and the origin is the average of its corners.
void cut_flat_base(const GEO::Mesh& source, const float cut_y, const float weld_distance, GEO::Mesh& mesh)
{
    enum class Side : unsigned int { below = 0, on, above };
    std::vector<glm::vec3> positions(source.vertices.nb());
    std::vector<Side>      sides    (source.vertices.nb());
    for (GEO::index_t v = 0, end = source.vertices.nb(); v < end; ++v) {
        const GEO::vec3f p = erhe::geometry::get_pointf(source.vertices, v);
        positions[v] = glm::vec3{p.x, p.y, p.z};
        // A vertex within the weld distance of the plane lies on it: no cluster of near-equal edge crossings forms around it.
        sides[v] = (p.y >= cut_y + weld_distance) ? Side::above : (p.y > cut_y - weld_distance) ? Side::on : Side::below;
    }
    std::vector<glm::vec3> points;
    for (GEO::index_t v = 0, end = source.vertices.nb(); v < end; ++v) {
        if (sides[v] == Side::above) {
            add_welded_point(points, positions[v], weld_distance);
        } else if (sides[v] == Side::on) {
            add_welded_point(points, glm::vec3{positions[v].x, cut_y, positions[v].z}, weld_distance);
        }
    }
    for (GEO::index_t f = 0, end = source.facets.nb(); f < end; ++f) {
        const GEO::index_t corner_count = source.facets.nb_vertices(f);
        for (GEO::index_t c = 0; c < corner_count; ++c) {
            const GEO::index_t a = source.facets.vertex(f, c);
            const GEO::index_t b = source.facets.vertex(f, (c + 1) % corner_count);
            // Each edge of the closed mesh once, from its lower vertex.
            if ((a > b) || !(((sides[a] == Side::above) && (sides[b] == Side::below)) || ((sides[a] == Side::below) && (sides[b] == Side::above)))) {
                continue;
            }
            const glm::vec3 pa = positions[a];
            const glm::vec3 pb = positions[b];
            const float     t  = (cut_y - pa.y) / (pb.y - pa.y);
            glm::vec3 crossing = pa + (t * (pb - pa));
            crossing.y = cut_y;
            add_welded_point(points, crossing, weld_distance);
        }
    }
    GEO::Mesh hull{3, true};
    if (!erhe::geometry::shapes::make_convex_hull(hull, points)) {
        return;
    }

    std::vector<glm::vec3> hull_positions(hull.vertices.nb());
    for (GEO::index_t v = 0, end = hull.vertices.nb(); v < end; ++v) {
        const GEO::vec3f p = erhe::geometry::get_pointf(hull.vertices, v);
        hull_positions[v] = glm::vec3{p.x, p.y, p.z};
    }
    const auto on_base = [&](const GEO::index_t v) -> bool { return hull_positions[v].y == cut_y; };

    // The base triangles' boundary, as next-vertex links in their winding order.
    std::vector<std::pair<GEO::index_t, GEO::index_t>> base_edges;
    for (GEO::index_t f = 0, end = hull.facets.nb(); f < end; ++f) {
        if (on_base(hull.facets.vertex(f, 0)) && on_base(hull.facets.vertex(f, 1)) && on_base(hull.facets.vertex(f, 2))) {
            for (GEO::index_t c = 0; c < 3; ++c) {
                base_edges.emplace_back(hull.facets.vertex(f, c), hull.facets.vertex(f, (c + 1) % 3));
            }
        }
    }
    std::vector<GEO::index_t> base_next(hull.vertices.nb(), GEO::NO_INDEX);
    GEO::index_t base_start = GEO::NO_INDEX;
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : base_edges) {
        const bool interior = std::find(base_edges.begin(), base_edges.end(), std::make_pair(edge.second, edge.first)) != base_edges.end();
        if (!interior) {
            base_next[edge.first] = edge.second;
            base_start = edge.first;
        }
    }
    std::vector<GEO::index_t> base_loop;
    for (GEO::index_t v = base_start; (v != GEO::NO_INDEX) && (base_loop.size() <= base_edges.size()); v = base_next[v]) {
        base_loop.push_back(v);
        if (base_next[v] == base_start) {
            break;
        }
    }

    glm::vec2 center{0.0f};
    for (const GEO::index_t v : base_loop) {
        center += glm::vec2{hull_positions[v].x, hull_positions[v].z};
    }
    center /= static_cast<float>(std::max<std::size_t>(1, base_loop.size()));

    Kit_mesh_builder          builder;
    std::vector<uint32_t>     builder_index(hull.vertices.nb(), std::numeric_limits<uint32_t>::max());
    const auto vertex = [&](const GEO::index_t v) -> uint32_t {
        if (builder_index[v] == std::numeric_limits<uint32_t>::max()) {
            const glm::vec3 p = hull_positions[v];
            builder_index[v] = builder.add_vertex(glm::vec3{p.x - center.x, on_base(v) ? 0.0f : (p.y - cut_y), p.z - center.y});
        }
        return builder_index[v];
    };
    for (GEO::index_t f = 0, end = hull.facets.nb(); f < end; ++f) {
        const GEO::index_t a = hull.facets.vertex(f, 0);
        const GEO::index_t b = hull.facets.vertex(f, 1);
        const GEO::index_t c = hull.facets.vertex(f, 2);
        if (on_base(a) && on_base(b) && on_base(c)) {
            continue;
        }
        builder.add_triangle(vertex(a), vertex(b), vertex(c));
    }
    std::vector<uint32_t> base_polygon;
    for (const GEO::index_t v : base_loop) {
        base_polygon.push_back(vertex(v));
    }
    builder.add_polygon(base_polygon);
    builder.write(mesh);
}

void generate_hull_rock(const Kit_parameter_values& values, const Rock_hull_recipe& recipe, GEO::Mesh& mesh)
{
    const float size        = values.get_float("size");
    const int   bevels      = std::clamp(values.get_int("bevels"), 0, static_cast<int>(rock_bevel_ratios.size()));
    const int   point_count = (bevels >= 2) ? recipe.beveled_twice_points : recipe.point_count;
    Kit_random random{values.get_seed()};
    std::vector<glm::vec3> points;
    make_rock_points(recipe, point_count, size, values.get_float("roughness"), random, points);

    std::unique_ptr<erhe::geometry::Geometry> current = std::make_unique<erhe::geometry::Geometry>("rock");
    if (!erhe::geometry::shapes::make_convex_hull(current->get_mesh(), points)) {
        return;
    }
    current->process({.flags = erhe::geometry::Geometry::process_flag_connect | erhe::geometry::Geometry::process_flag_build_edges});
    for (int pass = 0; pass < bevels; ++pass) {
        std::unique_ptr<erhe::geometry::Geometry> beveled = std::make_unique<erhe::geometry::Geometry>("rock");
        erhe::geometry::operation::chamfer3(*current, *beveled, rock_bevel_ratios[static_cast<std::size_t>(pass)]);
        current = std::move(beveled);
    }
    const GEO::Mesh& beveled = current->get_mesh();
    float low  = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    for (GEO::index_t v = 0, end = beveled.vertices.nb(); v < end; ++v) {
        const float y = erhe::geometry::get_pointf(beveled.vertices, v).y;
        low  = std::min(low, y);
        high = std::max(high, y);
    }
    cut_flat_base(beveled, low + (recipe.base_cut * (high - low)), 1.0e-3f * size, mesh);
}

void generate_rock(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form = values.get_enum("form");
    if (form == "blob") {
        generate_blob_rock(values, mesh);
        return;
    }
    generate_hull_rock(values, get_rock_hull_recipe(form), mesh);
}

auto land_mass_top_radius(const float base_radius, const float height) -> float
{
    return std::max(base_radius - height, 0.5f * base_radius);
}

void generate_land_mass(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float rb        = 0.5f * values.get_float("footprint");
    const float h         = values.get_float("height");
    const float roundness = values.get_float("roundness");
    const float rt        = land_mass_top_radius(rb, h);
    Kit_random random{values.get_seed()};
    Kit_blob   blob;
    constexpr int levels = 4;
    for (int i = 0; i < levels; ++i) {
        const float t       = static_cast<float>(i) / static_cast<float>(levels - 1);
        const float straight = 1.0f - t;
        const float rounded  = std::sqrt(std::max(0.0f, 1.0f - (t * t)));
        const float profile  = straight + (roundness * (rounded - straight));
        blob.rings.push_back(Kit_blob_ring{t * h, rt + ((rb - rt) * profile)});
    }
    blob.segments      = values.get_int("segments");
    blob.bottom_cap    = Kit_blob_cap::flat;
    blob.top_cap       = Kit_blob_cap::flat;
    blob.angle_jitter  = 0.15f;
    blob.vertex_jitter = 0.03f;
    blob.height_jitter = 0.04f * h;
    Kit_mesh_builder builder;
    builder.add_blob(blob, random);
    builder.write(mesh);
}

auto hill_ring_count(const int segments) -> int
{
    return std::max(2, segments / 4);
}

// Unjittered profile of a hill: a raised cosine, flat at the base and at the top.
auto hill_ring(const float radius, const float height, const int ring, const int ring_count) -> Kit_blob_ring
{
    const float t = static_cast<float>(ring) / static_cast<float>(ring_count);
    return Kit_blob_ring{
        .y      = 0.5f * height * (1.0f - std::cos(std::numbers::pi_v<float> * t)),
        .radius = radius * (1.0f - t)
    };
}

void generate_hill(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float r          = values.get_float("radius");
    const float h          = values.get_float("height");
    const int   segments   = values.get_int("segments");
    const int   ring_count = hill_ring_count(segments);
    Kit_random random{values.get_seed()};
    Kit_blob   blob;
    for (int i = 0; i < ring_count; ++i) {
        blob.rings.push_back(hill_ring(r, h, i, ring_count));
    }
    blob.segments      = segments;
    blob.bottom_cap    = Kit_blob_cap::flat;
    blob.top_cap       = Kit_blob_cap::point;
    blob.top_point_y   = h;
    blob.angle_jitter  = 0.1f;
    blob.vertex_jitter = 0.04f;
    blob.height_jitter = 0.25f * blob.rings[1].y;
    Kit_mesh_builder builder;
    builder.add_blob(blob, random);
    builder.write(mesh);
}

void generate_tree(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float trunk_height = values.get_float("trunk_height");
    const float d            = values.get_float("canopy_size");
    const bool  conical      = (values.get_enum("canopy") == "conical");
    const float trunk_radius = std::max(0.03f, 0.06f * d);
    Kit_random random{values.get_seed()};
    Kit_blob   blob;
    blob.segments      = values.get_int("segments");
    blob.angle_jitter  = 0.12f;
    blob.vertex_jitter = 0.06f;
    blob.apex_offset   = 0.1f;
    float trunk_top = trunk_height;
    if (conical) {
        const float hc = conical_canopy_height_per_size * d;
        blob.rings         = {{trunk_height, 0.5f * d}, {trunk_height + (0.45f * hc), 0.3f * d}};
        blob.bottom_cap    = Kit_blob_cap::flat;
        blob.top_cap       = Kit_blob_cap::point;
        blob.top_point_y   = trunk_height + hc;
        blob.height_jitter = 0.03f * hc;
        trunk_top          = trunk_height + (0.2f * hc);
    } else {
        const float hc = round_canopy_height_per_size * d;
        blob.rings          = {{trunk_height + (0.2f * hc), 0.42f * d}, {trunk_height + (0.55f * hc), 0.5f * d}, {trunk_height + (0.85f * hc), 0.33f * d}};
        blob.bottom_cap     = Kit_blob_cap::point;
        blob.top_cap        = Kit_blob_cap::point;
        blob.bottom_point_y = trunk_height;
        blob.top_point_y    = trunk_height + hc;
        blob.height_jitter  = 0.03f * hc;
        trunk_top           = trunk_height + (0.3f * hc);
    }
    // Two closed components: the trunk reaches into the canopy so no gap shows between them.
    Kit_mesh_builder builder;
    builder.set_slot(tree_bark_slot);
    builder.add_prism(tree_trunk_segments, trunk_radius, 0.0f, trunk_top, 0.0f);
    builder.set_slot(tree_foliage_slot);
    builder.add_blob(blob, random);
    builder.write(mesh);
}

void generate_bush(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float size = values.get_float("size");
    const float r    = 0.5f * size;
    const float h    = 0.6f * size;
    Kit_random random{values.get_seed()};
    Kit_blob   blob;
    blob.rings         = {{0.0f, 0.6f * r}, {0.35f * h, r}, {0.75f * h, 0.75f * r}};
    blob.segments      = values.get_int("segments");
    blob.bottom_cap    = Kit_blob_cap::flat;
    blob.top_cap       = Kit_blob_cap::point;
    blob.top_point_y   = h;
    blob.angle_jitter  = 0.2f;
    blob.vertex_jitter = 0.08f;
    blob.height_jitter = 0.05f * h;
    blob.apex_offset   = 0.15f;
    Kit_mesh_builder builder;
    builder.add_blob(blob, random);
    builder.write(mesh);
}

} // anonymous namespace

void add_nature_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "nature.rock";
        shape.kit             = "nature";
        shape.display_name    = "Rock";
        shape.description     =
            "Low-polygon rock resting on a flat base at its origin: a jittered blob, or the convex hull of a jittered Fibonacci "
            "sphere (angular: few points, worn: many, slab: flat) with zero to two bevel passes over its edges.";
        shape.tags            = {"obstacle", "decoration", "size:1x1"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Blob, angular hull, worn hull or flat slab hull", {"blob", "angular", "worn", "slab"}, 1)),
            from_seed_by_default(derive_float(make_length_parameter("size", "Diameter before roughness", 1.0f, Kit_default_scale::modules, 0.05f, 32.0f), 0.6f, 1.5f, 0.05f)),
            derive_float(make_ratio_parameter("roughness", "Amount of random jitter of the points or dents", 0.3f), 0.1f, 0.6f, 0.01f),
            derive_int(make_count_parameter("bevels", "Bevel passes over the hull's edges; the blob has none", 1, 0, 2), 0, 2),
            make_seed_parameter()
        };
        shape.material_slots  = {"rock"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 120;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_rock;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.land_mass";
        shape.kit             = "nature";
        shape.display_name    = "Land mass";
        shape.description     =
            "Blob with a rounded footprint, a flat walkable top and sloping sides; roundness 0 gives straight sides, 1 a rounded "
            "shoulder.";
        shape.tags            = {"walkable", "ground", "size:4x4x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("footprint", "Diameter of the base",       4.0f, Kit_default_scale::modules, 0.5f,  128.0f), 3.0f, 8.0f, 0.5f),
            derive_float(make_length_parameter("height",    "Height of the flat top",     1.0f, Kit_default_scale::modules, 0.05f, 32.0f), 0.5f, 2.0f, 0.25f),
            from_seed_by_default(derive_float(make_ratio_parameter("roundness", "Rounding of the side profile", 0.5f), 0.2f, 0.8f, 0.01f)),
            make_seed_parameter  (),
            make_count_parameter ("segments",  "Sides around the axis",      16, 3, 64)
        };
        shape.material_slots  = {"earth"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 128;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_land_mass;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float rb   = 0.5f * values.get_float("footprint");
            const float rt   = land_mass_top_radius(rb, values.get_float("height"));
            const float need = (2.0f * agent.radius) + kit_opening_margin;
            if (2.0f * rt < need) {
                return fmt::format("{}: flat top {} m across is narrower than the agent's {} m", values.get_shape()->id, 2.0f * rt, need);
            }
            return {};
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.hill";
        shape.kit             = "nature";
        shape.display_name    = "Hill / mound";
        shape.description     = "Round mound with a raised-cosine profile, flat at its rim and top; its sides are walkable.";
        shape.tags            = {"walkable", "ground", "size:6x6x1"};
        shape.parameters      = {
            // Derived height over radius stays at most 0.5, a slope of about 35 degrees (R2.10).
            derive_float(make_length_parameter("radius",   "Radius of the base",   3.0f, Kit_default_scale::modules, 0.5f,  128.0f), 2.5f, 6.0f, 0.5f),
            from_seed_by_default(derive_float(make_length_parameter("height",   "Height of the top",    1.0f, Kit_default_scale::modules, 0.05f, 32.0f), 0.5f, 1.25f, 0.05f)),
            make_seed_parameter  (),
            make_count_parameter ("segments", "Sides around the axis; rings are a quarter of it", 16, 3, 60)
        };
        shape.material_slots  = {"grass"};
        shape.normal_style    = Kit_normal_style::smooth;
        shape.triangle_budget = 128;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_hill;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float r          = values.get_float("radius");
            const float h          = values.get_float("height");
            const int   ring_count = hill_ring_count(values.get_int("segments"));
            float steepest = 0.0f;
            for (int i = 0; i < ring_count; ++i) {
                const Kit_blob_ring lower = hill_ring(r, h, i, ring_count);
                const Kit_blob_ring upper = hill_ring(r, h, i + 1, ring_count);
                const float slope = std::atan2(upper.y - lower.y, lower.radius - upper.radius) * (180.0f / std::numbers::pi_v<float>);
                steepest = std::max(steepest, slope);
            }
            if (steepest > agent.max_slope_degrees) {
                return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", values.get_shape()->id, steepest, agent.max_slope_degrees);
            }
            return {};
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.tree";
        shape.kit             = "nature";
        shape.display_name    = "Tree";
        shape.description     =
            "Six-sided trunk (slot bark) standing at the origin with one round or conical canopy (slot foliage); the trunk "
            "reaches into the canopy, so the mesh is two closed parts.";
        shape.tags            = {"obstacle", "decoration", "size:2x2x3"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("trunk_height", "Height of the canopy's base", 0.6f, Kit_default_scale::wall_heights, 0.1f, 32.0f), 0.4f, 0.8f, 0.05f)),
            from_seed_by_default(derive_float(make_length_parameter("canopy_size",  "Diameter of the canopy",      2.0f, Kit_default_scale::modules,      0.2f, 32.0f), 1.5f, 3.0f, 0.1f)),
            derive_enum(make_enum_parameter("canopy", "Canopy form", {"round", "conical"}, 0)),
            make_seed_parameter  (),
            make_count_parameter ("segments",     "Canopy sides around the axis", 8, 3, 32)
        };
        shape.material_slots  = {"bark", "foliage"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 68;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_tree;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.bush";
        shape.kit             = "nature";
        shape.display_name    = "Bush";
        shape.description     = "Low lumpy blob resting on a flat base at its origin.";
        shape.tags            = {"obstacle", "decoration", "size:1x1"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("size", "Diameter", 1.0f, Kit_default_scale::modules, 0.1f, 16.0f), 0.6f, 1.4f, 0.05f)),
            make_seed_parameter  (),
            make_count_parameter ("segments", "Sides around the axis", 8, 3, 32)
        };
        shape.material_slots  = {"foliage"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 48;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_bush;
        shapes.push_back(std::move(shape));
    }
    add_nature_terrain_shapes(shapes);
    add_nature_rock_shapes   (shapes);
    add_nature_plant_shapes  (shapes);
}

} // namespace editor
