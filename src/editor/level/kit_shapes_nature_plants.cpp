#include "level/kit_leaf.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/shapes/sweep.hpp"

#include <geogram/mesh/mesh.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace editor {

namespace {

constexpr uint32_t palm_bark_slot    = 0; // the trunk, nuts and dead leaves
constexpr uint32_t palm_foliage_slot = 1; // fronds, fan leaves and the crownshaft
constexpr float    palm_golden_angle = 2.39996323f;
constexpr float    palm_crown_reach  = 0.6f;  // the longest frond is at most this share of the crown's height, so no leaf reaches the ground
constexpr float    palm_crown_least  = 0.1f;  // and at least this share, so a frond keeps a size the mesh checks resolve

enum class Palm_crown : unsigned int {
    feather = 0,
    fan
};

// What a species sets (doc/editor/level_kit_shapes.md "Palm").
class Palm_species
{
public:
    float                  radius_per_height; // trunk radius at the base station, per meter of height
    float                  bend_factor;       // share of the bend parameter the trunk takes
    float                  ring_amplitude;    // every second trunk station is wider by this share
    int                    trunk_steps;
    std::vector<glm::vec2> taper;             // trunk radius along the trunk, (t, scale)
    float                  crownshaft;        // green crownshaft length, share of the height; 0 for none
    Palm_crown             crown;
    float                  elevation_low;     // the lowest frond's start above the horizontal, degrees
    float                  elevation_high;    // the youngest frond's
    float                  droop_factor;
    float                  frond_width;       // per meter of frond length
    float                  fold;
    int                    nuts;
};

[[nodiscard]] auto get_palm_species(const std::string_view species) -> Palm_species
{
    if (species == "date") {
        return Palm_species{0.045f, 0.35f, 0.16f, 12, {{0.0f, 1.2f}, {0.1f, 1.0f}, {1.0f, 0.95f}}, 0.0f, Palm_crown::feather, 15.0f, 70.0f, 0.5f, 0.13f, 0.35f, 0};
    }
    if (species == "royal") {
        return Palm_species{0.03f, 0.12f, 0.015f, 8, {{0.0f, 1.3f}, {0.1f, 1.0f}, {0.5f, 1.1f}, {1.0f, 0.85f}}, 0.14f, Palm_crown::feather, 5.0f, 50.0f, 0.9f, 0.18f, 0.8f, 0};
    }
    if (species == "fan") {
        return Palm_species{0.032f, 0.5f, 0.08f, 10, {{0.0f, 1.25f}, {0.1f, 1.0f}, {1.0f, 0.85f}}, 0.0f, Palm_crown::fan, -20.0f, 70.0f, 0.8f, 0.0f, 0.0f, 0};
    }
    if (species == "bottle") {
        return Palm_species{0.05f, 0.2f, 0.03f, 10, {{0.0f, 1.0f}, {0.2f, 1.45f}, {0.4f, 1.55f}, {0.7f, 1.0f}, {0.9f, 0.62f}, {1.0f, 0.58f}}, 0.11f, Palm_crown::feather, 5.0f, 50.0f, 0.8f, 0.16f, 0.5f, 0};
    }
    return Palm_species{0.022f, 1.0f, 0.05f, 10, {{0.0f, 1.3f}, {0.12f, 1.0f}, {1.0f, 0.75f}}, 0.0f, Palm_crown::feather, 0.0f, 45.0f, 1.0f, 0.16f, 0.6f, 3}; // coconut
}

[[nodiscard]] auto evaluate_keys(const std::vector<glm::vec2>& keys, const float t) -> float
{
    if (t <= keys.front().x) {
        return keys.front().y;
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t <= keys[i + 1].x) {
            const float f = (t - keys[i].x) / (keys[i + 1].x - keys[i].x);
            return keys[i].y + (f * (keys[i + 1].y - keys[i].y));
        }
    }
    return keys.back().y;
}

[[nodiscard]] auto regular_profile(const int sides, const float radius) -> std::vector<glm::vec2>
{
    std::vector<glm::vec2> profile;
    for (int i = 0; i < sides; ++i) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(sides);
        profile.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
    }
    return profile;
}

[[nodiscard]] auto cubic_tangent(const std::array<glm::vec3, 4>& p, const float t) -> glm::vec3
{
    const float s = 1.0f - t;
    return glm::normalize(((3.0f * s * s) * (p[1] - p[0])) + ((6.0f * s * t) * (p[2] - p[1])) + ((3.0f * t * t) * (p[3] - p[2])));
}

// A pleated fan leaf around center, spanning +-half_angle from u in the plane of u and v; a closed thin shell.
void add_fan_leaf(
    Kit_mesh_builder& builder,
    const glm::vec3&  center,
    const glm::vec3&  u,
    const glm::vec3&  v,
    const float       radius,
    const float       half_angle,
    const int         sectors,
    const float       thickness
)
{
    const glm::vec3 n      = glm::normalize(glm::cross(u, v));
    const glm::vec3 offset = (0.5f * thickness) * n;
    const float     pleat  = 0.07f * radius;
    std::vector<uint32_t> top;
    std::vector<uint32_t> bottom;
    for (int i = 0; i <= sectors; ++i) {
        const float     phi  = -half_angle + ((2.0f * half_angle) * static_cast<float>(i) / static_cast<float>(sectors));
        const float     lift = ((i % 2) == 0) ? pleat : -pleat;
        const glm::vec3 rim  = center + (radius * ((std::cos(phi) * u) + (std::sin(phi) * v))) + (lift * n);
        top   .push_back(builder.add_vertex(rim + offset));
        bottom.push_back(builder.add_vertex(rim - offset));
    }
    const uint32_t center_top    = builder.add_vertex(center + offset);
    const uint32_t center_bottom = builder.add_vertex(center - offset);
    const std::size_t last = static_cast<std::size_t>(sectors);
    for (std::size_t i = 0; i < last; ++i) {
        builder.add_triangle(center_top, top[i], top[i + 1]);
        builder.add_triangle(center_bottom, bottom[i + 1], bottom[i]);
        builder.add_quad(top[i + 1], top[i], bottom[i], bottom[i + 1]);
    }
    builder.add_quad(top[0], center_top, center_bottom, bottom[0]);
    builder.add_quad(center_top, top[last], bottom[last], center_bottom);
}

// One palm of a palm shape: where it stands, its sizes and the detail of its crown (the clump's and the shrub's crowns
// are lighter than the single palm's).
class Palm_tree
{
public:
    glm::vec3 base        {0.0f};
    float     height      {1.0f};
    float     bend        {0.0f};  // meters the crown stands off the base toward the heading
    float     heading     {0.0f};
    int       fronds      {8};
    float     frond_length{1.0f};
    float     droop       {0.0f};  // the species' droop factor applied
    int       sides       {6};
    int       frond_steps {5};
    int       fan_sectors {9};
    bool      skirt       {true};  // a fan crown's dead leaves
    float     lowest      {-1.0e9f}; // no frond control point lies lower: a low crown's fronds rest above the ground
    float     least_radius{0.08f};
};

void add_palm_tree(Kit_mesh_builder& builder, GEO::Mesh& scratch, const Palm_species& species, const Palm_tree& tree, Kit_random& frond_random)
{
    const float     height       = tree.height;
    const float     bend         = tree.bend;
    const int       frond_count  = tree.fronds;
    const float     frond_length = tree.frond_length;
    const float     droop        = tree.droop;
    const int       sides        = tree.sides;
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const glm::vec3 lean{std::cos(tree.heading), 0.0f, -std::sin(tree.heading)};
    const float     lowest = tree.lowest;
    const auto      lift   = [lowest](glm::vec3 p) -> glm::vec3 {
        p.y = std::max(p.y, lowest);
        return p;
    };

    const float crownshaft_length = species.crownshaft * height;
    const float trunk_height      = height - crownshaft_length;
    const float radius            = std::max(tree.least_radius, species.radius_per_height * height);

    // The trunk starts vertical, so its base ring lies flat at the base.
    const std::array<glm::vec3, 4> trunk_spine{
        tree.base,
        tree.base + glm::vec3{0.0f, trunk_height / 3.0f, 0.0f},
        tree.base + (0.25f * bend * lean) + glm::vec3{0.0f, 0.68f * trunk_height, 0.0f},
        tree.base + (bend * lean) + glm::vec3{0.0f, trunk_height, 0.0f}
    };
    builder.set_slot(palm_bark_slot);
    {
        erhe::geometry::shapes::Sweep_parameters trunk;
        trunk.profile     = regular_profile(sides, radius);
        trunk.spine       = std::vector<glm::vec3>{trunk_spine.begin(), trunk_spine.end()};
        trunk.spine_steps = species.trunk_steps;
        for (int i = 0; i <= species.trunk_steps; ++i) {
            const float t    = static_cast<float>(i) / static_cast<float>(species.trunk_steps);
            const float ring = ((i % 2) == 1) ? species.ring_amplitude : 0.0f;
            trunk.taper.emplace_back(t, evaluate_keys(species.taper, t) * (1.0f + ring));
        }
        add_kit_sweep(builder, trunk, scratch);
    }
    const glm::vec3 trunk_top     = trunk_spine[3];
    const glm::vec3 trunk_tangent = cubic_tangent(trunk_spine, 1.0f);
    const float     top_radius    = radius * species.taper.back().y;
    glm::vec3       crown         = trunk_top;
    if (crownshaft_length > 0.0f) {
        builder.set_slot(palm_foliage_slot);
        erhe::geometry::shapes::Sweep_parameters crownshaft;
        crownshaft.profile     = regular_profile(sides, 1.15f * top_radius);
        crownshaft.spine       = {trunk_top - ((0.1f * crownshaft_length) * trunk_tangent), trunk_top + (crownshaft_length * trunk_tangent)};
        crownshaft.spine_steps = 2;
        crownshaft.taper       = {{0.0f, 1.0f}, {0.5f, 1.08f}, {1.0f, 0.8f}};
        add_kit_sweep(builder, crownshaft, scratch);
        crown = trunk_top + (crownshaft_length * trunk_tangent);
    }

    const float     first_heading = frond_random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    for (int k = 0; k < frond_count; ++k) {
        // Rank 1 is the youngest frond at the top: it stands up and droops least.
        const float     rank        = (frond_count > 1) ? (static_cast<float>(k) / static_cast<float>(frond_count - 1)) : 1.0f;
        const float     angle       = first_heading + (palm_golden_angle * static_cast<float>(k)) + frond_random.next_range(-0.15f, 0.15f);
        const glm::vec3 outward{std::cos(angle), 0.0f, -std::sin(angle)};
        const float     elevation   = glm::radians(species.elevation_low + ((species.elevation_high - species.elevation_low) * rank) + frond_random.next_range(-6.0f, 6.0f));
        const glm::vec3 direction   = (std::cos(elevation) * outward) + (std::sin(elevation) * up);
        const float     length      = frond_length * (0.85f + (0.25f * frond_random.next_unit())) * (1.0f - (0.25f * rank));
        const float     sag         = std::clamp(droop * (1.3f - (0.8f * rank)), 0.0f, 1.0f);
        const glm::vec3 base        = crown + ((0.4f * top_radius) * outward) - ((0.04f * length * (1.0f - rank)) * up);
        if (species.crown == Palm_crown::feather) {
            Kit_leaf frond;
            frond.spine = {
                base,
                lift(base + ((0.35f * length) * direction)),
                lift(base + ((0.70f * length) * direction) - ((0.3f * sag * length) * up)),
                lift(base + ((0.85f * length * (1.0f - (0.2f * sag))) * direction) - ((0.7f * sag * length) * up))
            };
            frond.section    = Kit_leaf_section::folded;
            frond.width      = species.frond_width * length;
            frond.thickness  = std::max(0.01f, 0.006f * length);
            frond.fold       = species.fold;
            frond.widest_at  = 0.35f;
            frond.base_width = 0.12f;
            frond.steps      = tree.frond_steps;
            builder.set_slot(palm_foliage_slot);
            add_leaf(builder, frond, scratch);
        } else {
            // A fan leaf on a stalk; its plane tilts down by the droop.
            const float     stalk_length = 0.4f * length;
            const glm::vec3 leaf_center  = lift(base + (stalk_length * direction));
            Kit_leaf stalk;
            stalk.spine      = {base, lift(base + ((stalk_length / 3.0f) * direction)), lift(base + ((2.0f * stalk_length / 3.0f) * direction)), leaf_center};
            stalk.section    = Kit_leaf_section::thick;
            stalk.width      = std::max(0.03f, 0.025f * length);
            stalk.widest_at  = 0.1f;
            stalk.base_width = 1.0f;
            stalk.steps      = 2;
            builder.set_slot(palm_foliage_slot);
            add_leaf(builder, stalk, scratch);
            const float     tilt = elevation - glm::radians(10.0f + (30.0f * sag));
            const glm::vec3 u    = (std::cos(tilt) * outward) + (std::sin(tilt) * up);
            const glm::vec3 v    = glm::normalize(glm::cross(up, outward));
            add_fan_leaf(builder, leaf_center, u, v, 0.33f * length, glm::radians(100.0f), tree.fan_sectors, std::max(0.012f, 0.006f * length));
        }
    }

    if ((species.crown == Palm_crown::fan) && tree.skirt) {
        // The skirt: dead leaves hanging around the trunk below the crown.
        builder.set_slot(palm_bark_slot);
        const int skirt = std::max(3, frond_count / 3);
        for (int k = 0; k < skirt; ++k) {
            const float     angle   = first_heading + (2.0f * std::numbers::pi_v<float> * (static_cast<float>(k) + 0.5f) / static_cast<float>(skirt));
            const glm::vec3 outward{std::cos(angle), 0.0f, -std::sin(angle)};
            const float     reach   = std::min(0.3f * frond_length, 0.25f * trunk_height);
            const glm::vec3 center  = trunk_top + ((radius * 1.4f) * outward) - (reach * up);
            const glm::vec3 u       = glm::normalize((-0.95f * up) + (0.3f * outward));
            const glm::vec3 v       = glm::normalize(glm::cross(up, outward));
            add_fan_leaf(builder, center, u, v, std::min(0.3f * frond_length, 0.3f * trunk_height), glm::radians(70.0f), 6, 0.02f);
        }
    }

    // Nuts hang under the crown.
    builder.set_slot(palm_bark_slot);
    for (int k = 0; k < species.nuts; ++k) {
        const float     angle   = first_heading + (2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(species.nuts)) + 0.4f;
        const glm::vec3 outward{std::cos(angle), 0.0f, -std::sin(angle)};
        const float     size    = std::clamp(0.03f * height, 0.08f, 0.2f);
        const glm::vec3 top     = trunk_top + ((top_radius + (0.6f * size)) * outward) - ((0.2f * size) * up);
        erhe::geometry::shapes::Sweep_parameters nut;
        nut.profile     = regular_profile(6, size);
        nut.spine       = {top, top - ((2.2f * size) * up)};
        nut.spine_steps = 3;
        nut.taper       = {{0.0f, 0.5f}, {0.45f, 1.0f}, {1.0f, 0.0f}};
        add_kit_sweep(builder, nut, scratch);
    }
}

void generate_palm(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Palm_species species = get_palm_species(values.get_enum("species"));
    const float        height  = values.get_float("height");
    Kit_random trunk_random{values.get_seed(), "palm trunk"};
    Palm_tree tree;
    tree.height       = height;
    tree.bend         = values.get_float("bend") * species.bend_factor * height;
    tree.heading      = trunk_random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    tree.fronds       = values.get_int("fronds");
    tree.frond_length = std::clamp(values.get_float("frond_length"), palm_crown_least * height, palm_crown_reach * height);
    tree.droop        = values.get_float("droop") * species.droop_factor;
    tree.sides        = values.get_int("segments");
    Kit_mesh_builder builder;
    GEO::Mesh        scratch{3, true};
    Kit_random       frond_random{values.get_seed(), "palm fronds"};
    add_palm_tree(builder, scratch, species, tree, frond_random);
    builder.write(mesh);
}

constexpr int palm_clump_most_fronds = 40; // in all: with the stems the clump stays within the kit's ceiling

// The clump's and the shrub's species (doc/editor/level_kit_shapes.md "Palm clump and palm shrub"); the frond length and the
// trunk height are shares of a stem's height or of the shrub's size.
class Palm_habit
{
public:
    Palm_species species;
    float        trunk;        // the trunk's share of the stem height or the size
    float        frond_length; // share of the stem height or the size
    float        least_radius;
};

[[nodiscard]] auto get_clump_habit(const std::string_view species) -> Palm_habit
{
    if (species == "rhapis") {
        return Palm_habit{Palm_species{0.016f, 0.6f, 0.12f, 3, {{0.0f, 1.0f}, {1.0f, 0.85f}}, 0.0f, Palm_crown::fan, 15.0f, 75.0f, 0.5f, 0.0f, 0.0f, 0}, 1.0f, 0.42f, 0.015f};
    }
    if (species == "nipa") {
        return Palm_habit{Palm_species{0.4f, 0.1f, 0.0f, 2, {{0.0f, 1.2f}, {1.0f, 0.7f}}, 0.0f, Palm_crown::feather, 50.0f, 80.0f, 0.35f, 0.15f, 0.4f, 0}, 0.1f, 0.9f, 0.04f};
    }
    return Palm_habit{Palm_species{0.016f, 1.0f, 0.08f, 3, {{0.0f, 1.25f}, {0.15f, 1.0f}, {1.0f, 0.8f}}, 0.0f, Palm_crown::feather, 20.0f, 70.0f, 0.9f, 0.2f, 0.5f, 0}, 1.0f, 0.5f, 0.02f}; // areca
}

// No control point of a frond or fan lower than this: the leaf's own reach below its spine stays above the ground.
[[nodiscard]] auto palm_lowest(const Palm_species& species, const float frond_length) -> float
{
    if (species.crown == Palm_crown::fan) {
        return 0.02f + (0.22f * frond_length);
    }
    return 0.02f + (0.5f * species.frond_width * frond_length * std::max(species.fold, 0.3f)) + (0.01f * frond_length);
}

void generate_palm_clump(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Palm_habit habit  = get_clump_habit(values.get_enum("species"));
    const float      height = values.get_float("height");
    const int        stems  = values.get_int("stems");
    const float      spread = values.get_float("spread");
    const int        total  = std::min(stems * values.get_int("fronds"), palm_clump_most_fronds);
    const float      root   = std::clamp(0.05f * height, 0.08f, 0.6f);

    Kit_random stem_random {values.get_seed(), "palm clump stems"};
    Kit_random frond_random{values.get_seed(), "palm clump fronds"};
    Kit_mesh_builder builder;
    GEO::Mesh        scratch{3, true};
    const float first_heading = stem_random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    for (int i = 0; i < stems; ++i) {
        // The first stem stands in the middle at the full height; the others around it, shorter, leaning out.
        const float heading = first_heading + (palm_golden_angle * static_cast<float>(i)) + stem_random.next_range(-0.3f, 0.3f);
        const float offset  = (i == 0) ? 0.0f : (root * std::sqrt(stem_random.next_range(0.3f, 1.0f)));
        const float stem    = height * ((i == 0) ? 1.0f : stem_random.next_range(0.55f, 0.95f));
        const float lean    = spread * stem_random.next_range(0.25f, 0.6f);
        Palm_tree tree;
        tree.base         = glm::vec3{offset * std::cos(heading), 0.0f, -offset * std::sin(heading)};
        tree.height       = habit.trunk * stem;
        tree.bend         = lean * habit.species.bend_factor * tree.height;
        tree.heading      = heading;
        tree.fronds       = std::max(2, (total / stems) + ((i < (total % stems)) ? 1 : 0));
        tree.frond_length = habit.frond_length * stem;
        tree.droop        = habit.species.droop_factor * stem_random.next_range(0.4f, 0.8f);
        tree.sides        = 5;
        tree.frond_steps  = 3;
        tree.fan_sectors  = 5;
        tree.skirt        = false;
        tree.lowest       = palm_lowest(habit.species, tree.frond_length);
        tree.least_radius = habit.least_radius;
        add_palm_tree(builder, scratch, habit.species, tree, frond_random);
    }
    // The root ball the stems rise from.
    builder.set_slot(palm_bark_slot);
    builder.add_prism(7, root + std::max(0.05f, 0.4f * habit.least_radius * height), 0.0f, std::max(0.04f, 0.03f * height), 0.2f);
    builder.write(mesh);
}

// The palm shrub and the leaf plant: their topology follows from their integer values and integer draws alone;
// random floats only move vertices.

constexpr float     pi                 = std::numbers::pi_v<float>;
constexpr float     two_pi             = 2.0f * pi;
constexpr float     golden_angle       = 2.39996323f;
constexpr uint32_t  plant_bark_slot    = 0; // stems, stumps and dead leaves
constexpr uint32_t  plant_foliage_slot = 1;
constexpr uint32_t  plant_shoot_slot   = 2; // the youngest leaves
constexpr glm::vec3 y_axis{0.0f, 1.0f, 0.0f};

[[nodiscard]] auto around_y(const float angle) -> glm::vec3
{
    return glm::vec3{std::cos(angle), 0.0f, -std::sin(angle)};
}

[[nodiscard]] auto next_signed(Kit_random& random) -> float
{
    return random.next_range(-1.0f, 1.0f);
}

// Rank of leaf k of count: 0 for the oldest, outermost leaf, 1 for the youngest at the center.
[[nodiscard]] auto leaf_rank(const int k, const int count) -> float
{
    return (count > 1) ? (static_cast<float>(k) / static_cast<float>(count - 1)) : 1.0f;
}

// A leaf's midrib: an arc in the vertical plane of `out` whose direction turns from elevation_0 at the base to
// elevation_1 at the tip. Where it descends it descends by the share `descent`, which fit_leaf_arc() sets.
class Leaf_arc
{
public:
    glm::vec3 base       {0.0f};
    glm::vec3 out        {1.0f, 0.0f, 0.0f}; // unit, horizontal
    float     length     {1.0f};
    float     elevation_0{0.0f};             // radians above the horizontal
    float     elevation_1{0.0f};
    float     power      {1.0f};             // above 1 the base stays straight and the end bends
    float     twist      {0.0f};             // radians the section turns about the midrib by the tip
    float     descent    {1.0f};
};

class Leaf_frame
{
public:
    glm::vec3 center;
    glm::vec3 along;
    glm::vec3 up;
    glm::vec3 side; // up x along
};

[[nodiscard]] auto arc_elevation(const Leaf_arc& arc, const float s) -> float
{
    return arc.elevation_0 + ((arc.elevation_1 - arc.elevation_0) * std::pow(s, arc.power));
}

// The arc's direction of travel at s before normalization, (outward, upward): descents are scaled.
[[nodiscard]] auto arc_velocity(const Leaf_arc& arc, const float s) -> glm::vec2
{
    const float elevation = arc_elevation(arc, s);
    const float rise      = std::sin(elevation);
    return glm::vec2{std::cos(elevation), std::max(rise, 0.0f) + (arc.descent * std::min(rise, 0.0f))};
}

[[nodiscard]] auto arc_frame(const Leaf_arc& arc, const float s) -> Leaf_frame
{
    constexpr int steps = 12;
    const float ds = s / static_cast<float>(steps);
    glm::vec2 reach{0.0f};
    for (int k = 0; k < steps; ++k) {
        reach += (ds * arc.length) * arc_velocity(arc, (static_cast<float>(k) + 0.5f) * ds);
    }
    const glm::vec2 heading   = glm::normalize(arc_velocity(arc, s));
    const glm::vec3 along     = (heading.x * arc.out) + (heading.y * y_axis);
    const glm::vec3 flat_up   = (-heading.y * arc.out) + (heading.x * y_axis);
    const glm::vec3 flat_side = glm::cross(y_axis, arc.out);
    const float     turn      = arc.twist * s;
    const float     c         = std::cos(turn);
    const float     t         = std::sin(turn);
    return Leaf_frame{
        .center = arc.base + (reach.x * arc.out) + (reach.y * y_axis),
        .along  = along,
        .up     = (c * flat_up) - (t * flat_side),
        .side   = (c * flat_side) + (t * flat_up)
    };
}

// Sets the arc's descent so its tip ends no lower than lowest: the largest share that keeps it, a min of continuous terms.
void fit_leaf_arc(Leaf_arc& arc, const float lowest)
{
    constexpr int steps = 24;
    float ascent = 0.0f;
    float fall   = 0.0f;
    for (int k = 0; k < steps; ++k) {
        const float rise = std::sin(arc_elevation(arc, (static_cast<float>(k) + 0.5f) / static_cast<float>(steps)));
        ascent += std::max(rise, 0.0f);
        fall   += std::max(-rise, 0.0f);
    }
    ascent *= arc.length / static_cast<float>(steps);
    fall   *= arc.length / static_cast<float>(steps);
    const float room = std::max((arc.base.y + ascent) - lowest, 0.02f * arc.length);
    arc.descent = std::min(1.0f, room / std::max(fall, 1.0e-6f));
}

// Scales the points' heights about pivot_y so none lies under floor_y (pivot_y above it): one affine map.
void flatten_above(const std::span<glm::vec3> points, const float pivot_y, const float floor_y)
{
    float lowest = pivot_y;
    for (const glm::vec3& point : points) {
        lowest = std::min(lowest, point.y);
    }
    const float scale = std::min(1.0f, (pivot_y - floor_y) / std::max(pivot_y - lowest, 1.0e-6f));
    for (glm::vec3& point : points) {
        point.y = pivot_y + (scale * (point.y - pivot_y));
    }
}

// A closed strip along the arc: at each station the two edges, the midrib and the keel under it; pointed at both ends.
// The lifts raise the edges over the midrib (a V fold) or lower them.
void add_ribbon(
    Kit_mesh_builder&            builder,
    const Leaf_arc&              arc,
    const std::span<const float> at,
    const std::span<const float> half_widths,
    const std::span<const float> lifts,
    const float                  thickness
)
{
    std::vector<glm::vec3> points;
    points.reserve(4 * at.size());
    for (std::size_t k = 0; k < at.size(); ++k) {
        const Leaf_frame frame = arc_frame(arc, at[k]);
        const glm::vec3  edge  = half_widths[k] * frame.side;
        const glm::vec3  lift  = lifts[k] * frame.up;
        points.push_back(frame.center + edge + lift);
        points.push_back(frame.center);
        points.push_back(frame.center - edge + lift);
        points.push_back(frame.center - (thickness * frame.up));
    }
    const Leaf_frame first = arc_frame(arc, at.front());
    const glm::vec3  base  = first.center - ((0.5f * (half_widths.front() + thickness)) * first.along);
    builder.add_ring_solid(points, 4, base, arc_frame(arc, 1.0f).center);
}

// A stalk: a ribbon tapering to 0.6 of its half width, as thick as wide.
void add_stalk(Kit_mesh_builder& builder, const Leaf_arc& arc, const float half_width)
{
    constexpr std::array<float, 3> at{0.0f, 0.5f, 0.97f};
    constexpr std::array<float, 3> lifts{0.0f, 0.0f, 0.0f};
    const std::array<float, 3> half_widths{half_width, 0.8f * half_width, 0.6f * half_width};
    add_ribbon(builder, arc, at, half_widths, lifts, half_width);
}

constexpr int blade_station_max = 10;

// A stalk and a blade widening from it: widest between two shares of the blade's length, rounded to its tip.
class Blade
{
public:
    Leaf_arc arc;
    float    half_width    {0.1f};
    float    thickness     {0.01f};
    float    stalk         {0.0f};  // the stalk's share of the length
    float    stalk_width   {0.15f}; // the stalk's half width and the blade's at its base, share of half_width
    int      stalk_stations{0};
    int      blade_stations{5};
    float    widest_from   {0.3f};
    float    widest_to     {0.7f};
    float    fold_base     {0.3f};  // the edges' lift per meter of half width at the blade's base
    float    fold_tip      {-0.1f};
};

void add_blade(Kit_mesh_builder& builder, const Blade& blade)
{
    std::array<float, blade_station_max> at{};
    std::array<float, blade_station_max> half_widths{};
    std::array<float, blade_station_max> lifts{};
    std::size_t count = 0;
    for (int k = 0; k < blade.stalk_stations; ++k) {
        at[count]          = blade.stalk * static_cast<float>(k) / static_cast<float>(blade.stalk_stations);
        half_widths[count] = blade.stalk_width * blade.half_width;
        lifts[count]       = 0.0f;
        ++count;
    }
    for (int k = 0; k < blade.blade_stations; ++k) {
        const float x     = 0.94f * static_cast<float>(k) / static_cast<float>(blade.blade_stations - 1);
        const float swell = std::sin(0.5f * pi * std::min(1.0f, x / blade.widest_from));
        const float past  = std::max(x - blade.widest_to, 0.0f) / (1.0f - blade.widest_to);
        const float share = (blade.stalk_width + ((1.0f - blade.stalk_width) * swell)) * std::sqrt(1.0f - (past * past));
        at[count]          = blade.stalk + ((1.0f - blade.stalk) * x);
        half_widths[count] = blade.half_width * share;
        lifts[count]       = half_widths[count] * (blade.fold_base + ((blade.fold_tip - blade.fold_base) * x));
        ++count;
    }
    add_ribbon(
        builder, blade.arc, std::span<const float>{at.data(), count}, std::span<const float>{half_widths.data(), count},
        std::span<const float>{lifts.data(), count}, blade.thickness
    );
}

enum class Shell_rim : unsigned int {
    open = 0, // the rim runs from the first rim point to the last: walls close the two ends to the hub
    closed    // the rim is a loop around the hub
};

// A thin closed shell: triangles from top[0] over the rim points top[1 ..], which run counter-clockwise about the
// upper face's normal, the same moved by offset underneath, and walls between the two.
void add_shell(Kit_mesh_builder& builder, const std::span<const glm::vec3> top, const glm::vec3& offset, const Shell_rim rim)
{
    const uint32_t count = static_cast<uint32_t>(top.size());
    uint32_t hub = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t v = builder.add_vertex(top[i]);
        if (i == 0) {
            hub = v;
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        builder.add_vertex(top[i] + offset);
    }
    const uint32_t under     = hub + count;
    const uint32_t rim_count = count - 1;
    const uint32_t spans     = (rim == Shell_rim::closed) ? rim_count : (rim_count - 1);
    for (uint32_t i = 0; i < spans; ++i) {
        const uint32_t a = 1 + i;
        const uint32_t b = 1 + ((i + 1) % rim_count);
        builder.add_triangle(hub, hub + a, hub + b);
        builder.add_triangle(under, under + b, under + a);
        builder.add_triangle(hub + b, hub + a, under + a);
        builder.add_triangle(hub + b, under + a, under + b);
    }
    if (rim == Shell_rim::open) {
        builder.add_triangle(hub + 1, hub, under);
        builder.add_triangle(hub + 1, under, under + 1);
        builder.add_triangle(hub, hub + rim_count, under + rim_count);
        builder.add_triangle(hub, under + rim_count, under);
    }
}

constexpr int fan_finger_max = 9;

// A palmate leaf: fingers about the hub, each a ridge from the hub to its tip between two clefts.
class Fan
{
public:
    glm::vec3 hub;
    glm::vec3 direction; // of the middle finger; unit
    glm::vec3 normal;    // of the upper face; unit, perpendicular to the direction, never downward
    float     radius;
    float     half_angle;
    int       fingers;
    float     notch;     // the clefts' reach, share of the radius
    float     pleat;     // the clefts' drop under the hub's plane, share of the radius
    float     droop;     // the tips' drop, share of the radius, most at the fan's sides
    float     thickness;
    float     floor_y;
};

void add_fan(Kit_mesh_builder& builder, const Fan& fan)
{
    std::array<glm::vec3, (2 * fan_finger_max) + 2> top{};
    const glm::vec3 side      = glm::cross(fan.normal, fan.direction);
    const int       rim_count = (2 * fan.fingers) + 1;
    top[0] = fan.hub;
    for (int i = 0; i < rim_count; ++i) {
        const float u      = static_cast<float>(i - fan.fingers) / static_cast<float>(fan.fingers);
        const float angle  = fan.half_angle * u;
        const bool  is_tip = (i % 2) == 1;
        const float reach  = fan.radius * (is_tip ? (1.0f - (0.22f * u * u)) : fan.notch);
        const float drop   = fan.radius * (is_tip ? (fan.droop * (0.3f + (0.7f * u * u))) : fan.pleat);
        top[static_cast<std::size_t>(i + 1)] = fan.hub + (reach * ((std::cos(angle) * fan.direction) + (std::sin(angle) * side))) - (drop * fan.normal);
    }
    const std::span<glm::vec3> points{top.data(), static_cast<std::size_t>(rim_count + 1)};
    flatten_above(points, fan.hub.y, fan.floor_y);
    add_shell(builder, points, -fan.thickness * fan.normal, Shell_rim::open);
}

// A heart-shaped blade hanging from the hub, where its stalk meets it between the two lobes.
class Heart
{
public:
    glm::vec3 hub;
    glm::vec3 direction; // toward the tip; unit
    glm::vec3 normal;    // of the upper face; unit, perpendicular to the direction, never downward
    float     length;
    float     width;     // factor on the outline's width
    float     thickness;
    float     floor_y;
};

void add_heart(Kit_mesh_builder& builder, const Heart& heart)
{
    class Outline_point { public: float forward; float lateral; };
    constexpr std::array<Outline_point, 10> outline{{
        {0.80f, 0.0f}, {0.45f, 0.40f}, {0.05f, 0.50f}, {-0.28f, 0.36f}, {-0.31f, 0.13f},
        {-0.07f, 0.0f}, {-0.31f, -0.13f}, {-0.28f, -0.36f}, {0.05f, -0.50f}, {0.45f, -0.40f}
    }};
    std::array<glm::vec3, outline.size() + 1> top{};
    const glm::vec3 side = glm::cross(heart.normal, heart.direction);
    top[0] = heart.hub;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const float forward = outline[i].forward * heart.length;
        const float lateral = outline[i].lateral * heart.length * heart.width;
        // The halves rise from the midrib, the tip hangs and the lobes stand up behind the stalk.
        const float cup     = (0.24f * std::abs(lateral)) - (0.3f * outline[i].forward * std::abs(outline[i].forward) * heart.length);
        top[i + 1] = heart.hub + (forward * heart.direction) + (lateral * side) + (cup * heart.normal);
    }
    flatten_above(top, heart.hub.y, heart.floor_y);
    add_shell(builder, top, -heart.thickness * heart.normal, Shell_rim::closed);
}

enum class Frond_outline : unsigned int {
    feather = 0, // leaflets longest in the middle: a palm's or a cycad's frond
    fern         // longest near the base, shortening to the tip
};

// A pinnate frond: a three-sided rachis along the arc and pairs of leaflets, each a low tetrahedron with a ridge.
class Frond
{
public:
    Leaf_arc      arc;
    Frond_outline outline       {Frond_outline::feather};
    int           pairs         {8};
    float         rachis_width  {0.01f}; // half width at the base
    float         leaflet_length{0.1f};  // of the longest
    float         leaflet_width {0.8f};  // a leaflet's root, share of the spacing
    float         rise          {0.0f};  // radians the leaflets stand out of the rachis' plane
    float         sweep         {0.3f};  // radians they point toward the tip
    float         sag           {0.0f};  // their tips' drop, share of their length
    float         first         {0.15f}; // where the leaflets start along the rachis
};

void add_frond(Kit_mesh_builder& builder, const Frond& frond)
{
    constexpr std::array<float, 5> at{0.0f, 0.25f, 0.5f, 0.75f, 0.97f};
    std::array<glm::vec3, 3 * at.size()> rachis{};
    for (std::size_t k = 0; k < at.size(); ++k) {
        const Leaf_frame frame = arc_frame(frond.arc, at[k]);
        const float      width = frond.rachis_width * (1.0f - (0.7f * at[k]));
        rachis[(3 * k) + 0] = frame.center + (width * frame.side);
        rachis[(3 * k) + 1] = frame.center - (width * frame.side);
        rachis[(3 * k) + 2] = frame.center - ((1.2f * width) * frame.up);
    }
    const Leaf_frame start = arc_frame(frond.arc, 0.0f);
    builder.add_ring_solid(rachis, 3, start.center - (frond.rachis_width * start.along), arc_frame(frond.arc, 1.0f).center);

    const float covered = 0.96f - frond.first;
    const float spacing = covered * frond.arc.length / static_cast<float>(frond.pairs);
    const float root    = 0.5f * frond.leaflet_width * spacing;
    for (int k = 0; k < frond.pairs; ++k) {
        const float      x      = (static_cast<float>(k) + 0.5f) / static_cast<float>(frond.pairs);
        const float      s      = frond.first + (covered * x);
        const Leaf_frame frame  = arc_frame(frond.arc, s);
        const float      share  = (frond.outline == Frond_outline::fern)
            ? ((1.0f - (0.9f * x)) * (0.5f + (0.5f * std::min(1.0f, x / 0.18f))))
            : (0.42f + (0.58f * std::sin(pi * (0.1f + (0.86f * x)))));
        const float      length = frond.leaflet_length * share;
        const float      sweep  = frond.sweep + (0.4f * x);
        const float      inset  = 0.3f * frond.rachis_width * (1.0f - (0.7f * s));
        for (int side_index = 0; side_index < 2; ++side_index) {
            const float     sign      = (side_index == 0) ? 1.0f : -1.0f;
            const glm::vec3 lateral   = sign * frame.side;
            const glm::vec3 direction = (std::cos(frond.rise) * ((std::cos(sweep) * lateral) + (std::sin(sweep) * frame.along))) + (std::sin(frond.rise) * frame.up);
            const glm::vec3 middle    = frame.center + (inset * lateral);
            const glm::vec3 drop      = (frond.sag * length) * y_axis;
            const glm::vec3 face      = glm::normalize(sign * glm::cross(frame.along, direction));
            // The root's ends in the order that winds the upper faces counter-clockwise on either side of the rachis.
            const uint32_t a     = builder.add_vertex(middle - ((sign * root) * frame.along));
            const uint32_t b     = builder.add_vertex(middle + ((sign * root) * frame.along));
            const uint32_t tip   = builder.add_vertex(middle + (length * direction) - drop);
            const uint32_t ridge = builder.add_vertex(middle + ((0.42f * length) * direction) - (0.42f * drop) + ((0.6f * root) * face));
            builder.add_triangle(a, b, ridge);
            builder.add_triangle(b, tip, ridge);
            builder.add_triangle(tip, a, ridge);
            builder.add_triangle(a, tip, b);
        }
    }
}

// A fern's unrolling young frond: a stalk curled in at its end.
void add_crozier(Kit_mesh_builder& builder, const Leaf_arc& arc, const float half_width)
{
    constexpr std::array<float, 7> at{0.0f, 0.22f, 0.42f, 0.58f, 0.72f, 0.84f, 0.94f};
    constexpr std::array<float, 7> lifts{};
    std::array<float, 7> half_widths{};
    for (std::size_t k = 0; k < at.size(); ++k) {
        half_widths[k] = half_width * (1.0f - (0.45f * at[k]));
    }
    add_ribbon(builder, arc, at, half_widths, lifts, 1.2f * half_width);
}

class Stem_station
{
public:
    float t;      // share of the height
    float radius; // share of the stem's radius
    float flare;  // share of the radius the root lobes take
};

// Rings from the ground up, the base a fan about a vertex on the origin; scales alternate the ring points in and out.
// Returns the top ring's center.
auto add_stem(
    Kit_mesh_builder&                   builder,
    Kit_random&                         random,
    const std::span<const Stem_station> stations,
    const int                           sides,
    const float                         radius,
    const float                         height,
    const glm::vec3&                    lean,
    const float                         scales
) -> glm::vec3
{
    std::vector<float> roots(static_cast<std::size_t>(sides));
    for (float& root : roots) {
        root = next_signed(random);
    }
    std::vector<glm::vec3> points;
    points.reserve(stations.size() * static_cast<std::size_t>(sides));
    for (std::size_t r = 0; r < stations.size(); ++r) {
        const Stem_station& station = stations[r];
        const glm::vec3     center{lean.x * station.t * station.t, station.t * height, lean.z * station.t * station.t};
        for (int i = 0; i < sides; ++i) {
            const glm::vec2 circle = get_kit_circle_point(i, sides);
            const float     scale  = 1.0f + (((((static_cast<std::size_t>(i) + r) % 2) == 0) ? scales : -scales));
            const float     reach  = radius * station.radius * scale * (1.0f + (station.flare * roots[static_cast<std::size_t>(i)]));
            points.push_back(center + glm::vec3{reach * circle.x, 0.0f, reach * circle.y});
        }
    }
    const glm::vec3 top{lean.x, height, lean.z};
    builder.add_ring_solid(points, sides, glm::vec3{0.0f}, top + glm::vec3{0.0f, 0.5f * radius * stations.back().radius, 0.0f});
    return top;
}

//
// Palm shrub: a low crown of palm leaves on a stub at the ground.
//

constexpr std::array<Stem_station, 3> shrub_stub   {{{0.0f, 1.25f, 0.25f}, {0.5f, 1.0f, 0.1f}, {1.0f, 0.7f, 0.0f}}};
constexpr std::array<Stem_station, 4> shrub_caudex {{{0.0f, 0.95f, 0.1f}, {0.3f, 1.15f, 0.0f}, {0.7f, 1.1f, 0.0f}, {1.0f, 0.75f, 0.0f}}};
constexpr std::array<Stem_station, 3> shrub_stem   {{{0.0f, 1.5f, 0.3f}, {0.2f, 1.0f, 0.0f}, {1.0f, 0.8f, 0.0f}}};

// An unopened spear leaf standing in the crown.
void add_spear(Kit_mesh_builder& builder, const glm::vec3& base, const glm::vec3& out, const float length, const float half_width)
{
    Blade spear;
    spear.arc.base        = base;
    spear.arc.out         = out;
    spear.arc.length      = length;
    spear.arc.elevation_0 = glm::radians(86.0f);
    spear.arc.elevation_1 = glm::radians(76.0f);
    spear.half_width      = half_width;
    spear.thickness       = 0.5f * half_width;
    spear.stalk_width     = 0.5f;
    spear.blade_stations  = 4;
    spear.widest_from     = 0.35f;
    spear.widest_to       = 0.55f;
    spear.fold_base       = 0.9f;
    spear.fold_tip        = 0.9f;
    add_blade(builder, spear);
}

// Palmetto: pleated fans on long stalks, the old ones low and drooping, a spear in the middle, dead fans on the ground.
void add_palmetto(Kit_mesh_builder& builder, Kit_random& random, Kit_random& cells, const float size, const int fronds)
{
    const float floor_y = 0.03f * size;
    builder.set_slot(plant_bark_slot);
    static_cast<void>(add_stem(builder, random, shrub_stub, 7, 0.075f * size, 0.14f * size, glm::vec3{0.0f}, 0.06f));
    const float first = random.next_range(0.0f, two_pi);
    builder.set_slot(plant_foliage_slot);
    for (int k = 0; k < fronds; ++k) {
        const float rank           = leaf_rank(k, fronds);
        const float azimuth_draw   = next_signed(random);
        const float elevation_draw = next_signed(random);
        const float length_draw    = next_signed(random);
        const float radius_draw    = next_signed(random);
        const float tilt_draw      = next_signed(random);
        const float radius         = size * 0.36f * (1.0f - (0.25f * rank)) * (1.0f + (0.1f * radius_draw));
        Leaf_arc stalk;
        stalk.out         = around_y(first + (golden_angle * static_cast<float>(k)) + (0.25f * azimuth_draw));
        stalk.base        = glm::vec3{0.0f, 0.10f * size, 0.0f} + ((0.04f * size) * stalk.out);
        stalk.length      = size * (0.46f + (0.06f * length_draw)) * (1.0f - (0.2f * rank));
        stalk.elevation_0 = glm::radians(28.0f + (54.0f * rank) + (6.0f * elevation_draw));
        stalk.elevation_1 = stalk.elevation_0 - glm::radians(18.0f - (10.0f * rank));
        stalk.power       = 1.5f;
        fit_leaf_arc(stalk, floor_y + (0.3f * radius));
        add_stalk(builder, stalk, 0.012f * size);
        const Leaf_frame tip  = arc_frame(stalk, 1.0f);
        const float      tilt = glm::radians(20.0f + (30.0f * (1.0f - rank)) + (8.0f * tilt_draw));
        add_fan(builder, Fan{
            .hub        = tip.center + ((0.012f * size) * tip.up),
            .direction  = (std::cos(tilt) * tip.along) - (std::sin(tilt) * tip.up),
            .normal     = (std::cos(tilt) * tip.up) + (std::sin(tilt) * tip.along),
            .radius     = radius,
            .half_angle = glm::radians(112.0f - (30.0f * rank)),
            .fingers    = 8,
            .notch      = 0.56f,
            .pleat      = 0.11f,
            .droop      = 0.12f + (0.3f * (1.0f - rank)),
            .thickness  = 0.006f * size,
            .floor_y    = floor_y
        });
    }
    builder.set_slot(plant_shoot_slot);
    const float spear_azimuth = random.next_range(0.0f, two_pi);
    add_spear(builder, glm::vec3{0.0f, 0.10f * size, 0.0f}, around_y(spear_azimuth), 0.5f * size, 0.035f * size);
    builder.set_slot(plant_bark_slot);
    const int dead_count = 1 + static_cast<int>(cells.next_u32() % 2u);
    for (int k = 0; k < dead_count; ++k) {
        const float azimuth    = random.next_range(0.0f, two_pi);
        const float droop_draw = random.next_unit();
        Leaf_arc stalk;
        stalk.out         = around_y(azimuth);
        stalk.base        = glm::vec3{0.0f, 0.09f * size, 0.0f} + ((0.05f * size) * stalk.out);
        stalk.length      = 0.3f * size;
        stalk.elevation_0 = glm::radians(-5.0f - (10.0f * droop_draw));
        stalk.elevation_1 = stalk.elevation_0 - glm::radians(15.0f);
        fit_leaf_arc(stalk, 1.6f * floor_y);
        add_stalk(builder, stalk, 0.011f * size);
        const Leaf_frame tip  = arc_frame(stalk, 1.0f);
        const float      tilt = glm::radians(30.0f);
        add_fan(builder, Fan{
            .hub        = tip.center + ((0.012f * size) * tip.up),
            .direction  = (std::cos(tilt) * tip.along) - (std::sin(tilt) * tip.up),
            .normal     = (std::cos(tilt) * tip.up) + (std::sin(tilt) * tip.along),
            .radius     = 0.26f * size,
            .half_angle = glm::radians(55.0f),
            .fingers    = 5,
            .notch      = 0.6f,
            .pleat      = 0.08f,
            .droop      = 0.3f,
            .thickness  = 0.006f * size,
            .floor_y    = 0.5f * floor_y
        });
    }
}

// Cycad: a scaly barrel carrying a rosette of stiff feather fronds and a flush of new ones standing in the middle.
void add_cycad(Kit_mesh_builder& builder, Kit_random& random, Kit_random& cells, const float size, const int fronds)
{
    const float floor_y = 0.03f * size;
    builder.set_slot(plant_bark_slot);
    const glm::vec3 crown = add_stem(builder, random, shrub_caudex, 8, 0.11f * size, 0.24f * size, glm::vec3{0.0f}, 0.08f);
    const float first = random.next_range(0.0f, two_pi);
    const int   pairs = (fronds > 12) ? 8 : 11;
    builder.set_slot(plant_foliage_slot);
    for (int k = 0; k < fronds; ++k) {
        const float rank           = leaf_rank(k, fronds);
        const float azimuth_draw   = next_signed(random);
        const float elevation_draw = next_signed(random);
        const float length_draw    = next_signed(random);
        Frond frond;
        frond.arc.out         = around_y(first + (golden_angle * static_cast<float>(k)) + (0.2f * azimuth_draw));
        frond.arc.base        = crown - glm::vec3{0.0f, 0.03f * size, 0.0f} + ((0.05f * size) * frond.arc.out);
        frond.arc.length      = size * 0.66f * (1.0f + (0.08f * length_draw)) * (1.0f - (0.22f * rank));
        frond.arc.elevation_0 = glm::radians(14.0f + (56.0f * rank) + (5.0f * elevation_draw));
        frond.arc.elevation_1 = frond.arc.elevation_0 - glm::radians(58.0f - (22.0f * rank));
        frond.arc.power       = 1.3f;
        frond.pairs           = pairs;
        frond.rachis_width    = 0.011f * size;
        frond.leaflet_length  = 0.2f * frond.arc.length;
        frond.leaflet_width   = 0.78f;
        frond.rise            = glm::radians(26.0f);
        frond.sweep           = glm::radians(14.0f);
        frond.first           = 0.14f;
        fit_leaf_arc(frond.arc, floor_y + (0.5f * frond.leaflet_length));
        add_frond(builder, frond);
    }
    builder.set_slot(plant_shoot_slot);
    const int   flush_count = 2 + static_cast<int>(cells.next_u32() % 2u);
    const float flush_first = random.next_range(0.0f, two_pi);
    for (int k = 0; k < flush_count; ++k) {
        const float azimuth_draw = next_signed(random);
        const float length_draw  = random.next_unit();
        Frond frond;
        frond.arc.out         = around_y(flush_first + (two_pi * (static_cast<float>(k) + (0.2f * azimuth_draw)) / static_cast<float>(flush_count)));
        frond.arc.base        = crown - glm::vec3{0.0f, 0.02f * size, 0.0f} + ((0.02f * size) * frond.arc.out);
        frond.arc.length      = size * (0.36f + (0.1f * length_draw));
        frond.arc.elevation_0 = glm::radians(84.0f);
        frond.arc.elevation_1 = glm::radians(62.0f);
        frond.pairs           = 5;
        frond.rachis_width    = 0.009f * size;
        frond.leaflet_length  = 0.17f * frond.arc.length;
        frond.leaflet_width   = 0.6f;
        frond.rise            = glm::radians(50.0f);
        frond.sweep           = glm::radians(35.0f);
        frond.first           = 0.2f;
        add_frond(builder, frond);
    }
}

// Seedling: a young palm, a few pleated undivided leaves standing on a thin stem.
void add_seedling(Kit_mesh_builder& builder, Kit_random& random, const float size, const int fronds)
{
    const float floor_y = 0.03f * size;
    builder.set_slot(plant_bark_slot);
    const glm::vec3 crown = add_stem(builder, random, shrub_stem, 6, 0.035f * size, 0.16f * size, glm::vec3{0.0f}, 0.0f);
    const float first = random.next_range(0.0f, two_pi);
    builder.set_slot(plant_foliage_slot);
    for (int k = 0; k < fronds; ++k) {
        const float rank           = leaf_rank(k, fronds);
        const float azimuth_draw   = next_signed(random);
        const float elevation_draw = next_signed(random);
        const float length_draw    = next_signed(random);
        const float tilt_draw      = next_signed(random);
        const float radius         = size * 0.52f * (1.0f + (0.1f * length_draw)) * (1.0f - (0.15f * rank));
        Leaf_arc stalk;
        stalk.out         = around_y(first + (golden_angle * static_cast<float>(k)) + (0.25f * azimuth_draw));
        stalk.base        = crown - glm::vec3{0.0f, 0.03f * size, 0.0f} + ((0.015f * size) * stalk.out);
        stalk.length      = size * 0.26f * (1.0f - (0.2f * rank));
        stalk.elevation_0 = glm::radians(50.0f + (34.0f * rank) + (5.0f * elevation_draw));
        stalk.elevation_1 = stalk.elevation_0 - glm::radians(10.0f);
        fit_leaf_arc(stalk, floor_y + (0.2f * radius));
        add_stalk(builder, stalk, 0.011f * size);
        const Leaf_frame tip  = arc_frame(stalk, 1.0f);
        const float      tilt = glm::radians(6.0f + (16.0f * (1.0f - rank)) + (4.0f * tilt_draw));
        add_fan(builder, Fan{
            .hub        = tip.center + ((0.012f * size) * tip.up),
            .direction  = (std::cos(tilt) * tip.along) - (std::sin(tilt) * tip.up),
            .normal     = (std::cos(tilt) * tip.up) + (std::sin(tilt) * tip.along),
            .radius     = radius,
            .half_angle = glm::radians(27.0f - (7.0f * rank)),
            .fingers    = 4,
            .notch      = 0.86f,
            .pleat      = 0.045f,
            .droop      = 0.08f + (0.2f * (1.0f - rank)),
            .thickness  = 0.006f * size,
            .floor_y    = floor_y
        });
    }
    builder.set_slot(plant_shoot_slot);
    const float spear_azimuth = random.next_range(0.0f, two_pi);
    add_spear(builder, crown - glm::vec3{0.0f, 0.03f * size, 0.0f}, around_y(spear_azimuth), 0.5f * size, 0.028f * size);
}

void generate_palm_shrub(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view species = values.get_enum("species");
    const float            size    = values.get_float("size");
    const int              fronds  = values.get_int("fronds");
    Kit_random random{values.get_seed(), "palm shrub v2"};       // moves vertices
    Kit_random cells {values.get_seed(), "palm shrub v2 cells"}; // decides the topology: integer draws only
    Kit_mesh_builder builder;
    if (species == "cycad") {
        add_cycad(builder, random, cells, size, fronds);
    } else if (species == "seedling") {
        add_seedling(builder, random, size, fronds);
    } else {
        add_palmetto(builder, random, cells, size, fronds);
    }
    builder.write(mesh);
}

//
// Leaf plant: large leaves on stalks about a stem or a stump.
//

constexpr std::array<Stem_station, 3> plant_mound      {{{0.0f, 1.2f, 0.25f}, {0.55f, 0.95f, 0.1f}, {1.0f, 0.5f, 0.0f}}};
constexpr std::array<Stem_station, 4> plant_pseudostem {{{0.0f, 1.35f, 0.12f}, {0.1f, 1.1f, 0.04f}, {0.55f, 0.9f, 0.0f}, {1.0f, 0.66f, 0.0f}}};
constexpr std::array<Stem_station, 5> plant_fern_trunk {{{0.0f, 1.5f, 0.25f}, {0.08f, 1.1f, 0.1f}, {0.5f, 0.95f, 0.0f}, {0.88f, 1.0f, 0.0f}, {1.0f, 1.25f, 0.0f}}};

// What every form of the leaf plant takes from the values.
class Leaf_plant
{
public:
    float height;
    int   leaves;
    float droop;
    float leaf_width;
    float floor_y;
};

// A young leaf still rolled up, standing at the crown.
void add_rolled_leaf(Kit_mesh_builder& builder, Kit_random& random, const glm::vec3& base, const float length, const float half_width)
{
    const float azimuth = random.next_range(0.0f, two_pi);
    builder.set_slot(plant_shoot_slot);
    add_spear(builder, base, around_y(azimuth), length, half_width);
}

// Banana: broad oblong leaves arching from a stout stem, a rolled new leaf, dry leaves hanging down the stem.
void add_banana(Kit_mesh_builder& builder, Kit_random& random, Kit_random& cells, const Leaf_plant& plant)
{
    const float     height       = plant.height;
    const float     stem_height  = 0.45f * height;
    const float     lean_heading = random.next_range(0.0f, two_pi);
    const float     lean_draw    = random.next_range(0.0f, 0.08f);
    builder.set_slot(plant_bark_slot);
    const glm::vec3 crown = add_stem(builder, random, plant_pseudostem, 7, 0.055f * height, stem_height, (lean_draw * stem_height) * around_y(lean_heading), 0.0f);
    const float first = random.next_range(0.0f, two_pi);
    builder.set_slot(plant_foliage_slot);
    for (int k = 0; k < plant.leaves; ++k) {
        const float rank           = leaf_rank(k, plant.leaves);
        const float azimuth_draw   = next_signed(random);
        const float elevation_draw = next_signed(random);
        const float length_draw    = random.next_unit();
        const float twist_draw     = next_signed(random);
        Blade blade;
        blade.arc.out         = around_y(first + (golden_angle * static_cast<float>(k)) + (0.2f * azimuth_draw));
        blade.arc.base        = crown - glm::vec3{0.0f, 0.04f * height, 0.0f} + ((0.025f * height) * blade.arc.out);
        blade.arc.length      = 0.6f * height * (0.8f + (0.25f * length_draw)) * (0.82f + (0.18f * rank));
        blade.arc.elevation_0 = glm::radians(30.0f + (52.0f * rank) + (6.0f * elevation_draw));
        blade.arc.elevation_1 = blade.arc.elevation_0 - glm::radians(35.0f + (95.0f * plant.droop * (1.15f - (0.6f * rank))));
        blade.arc.power       = 1.7f;
        blade.arc.twist       = 0.35f * twist_draw;
        blade.half_width      = 0.5f * plant.leaf_width * blade.arc.length;
        blade.thickness       = 0.014f * blade.arc.length;
        blade.stalk           = 0.14f;
        blade.stalk_width     = 0.14f;
        blade.stalk_stations  = 1;
        blade.blade_stations  = 6;
        blade.widest_from     = 0.28f;
        blade.widest_to       = 0.72f;
        blade.fold_base       = 0.45f;
        blade.fold_tip        = -0.2f;
        fit_leaf_arc(blade.arc, plant.floor_y + (0.5f * blade.half_width));
        add_blade(builder, blade);
    }
    add_rolled_leaf(builder, random, crown - glm::vec3{0.0f, 0.03f * height, 0.0f}, 0.5f * height, 0.04f * height);
    builder.set_slot(plant_bark_slot);
    const int dead_count = 1 + static_cast<int>(cells.next_u32() % 2u);
    for (int k = 0; k < dead_count; ++k) {
        const float azimuth = random.next_range(0.0f, two_pi);
        Blade blade;
        blade.arc.out         = around_y(azimuth);
        blade.arc.base        = glm::vec3{0.85f * crown.x, 0.92f * crown.y, 0.85f * crown.z} + ((0.035f * height) * blade.arc.out);
        blade.arc.length      = 0.3f * height;
        blade.arc.elevation_0 = glm::radians(-35.0f);
        blade.arc.elevation_1 = glm::radians(-95.0f);
        blade.arc.power       = 0.6f;
        blade.half_width      = 0.055f * height;
        blade.thickness       = 0.012f * blade.arc.length;
        blade.stalk_width     = 0.25f;
        blade.blade_stations  = 4;
        blade.fold_base       = -0.3f;
        blade.fold_tip        = -0.5f;
        fit_leaf_arc(blade.arc, plant.floor_y + (0.06f * height));
        add_blade(builder, blade);
    }
}

// Taro: heart-shaped blades hanging from the tips of long stalks that rise from the ground.
void add_taro(Kit_mesh_builder& builder, Kit_random& random, const Leaf_plant& plant)
{
    const float height = plant.height;
    builder.set_slot(plant_bark_slot);
    static_cast<void>(add_stem(builder, random, plant_mound, 6, 0.06f * height, 0.07f * height, glm::vec3{0.0f}, 0.0f));
    const float first = random.next_range(0.0f, two_pi);
    builder.set_slot(plant_foliage_slot);
    for (int k = 0; k < plant.leaves; ++k) {
        const float rank           = leaf_rank(k, plant.leaves);
        const float azimuth_draw   = next_signed(random);
        const float elevation_draw = next_signed(random);
        const float length_draw    = random.next_unit();
        const float hang_draw      = next_signed(random);
        const float grown          = (0.78f + (0.22f * length_draw)) * (1.0f - (0.25f * rank));
        const float blade_length   = 0.42f * height * grown;
        Leaf_arc stalk;
        stalk.out         = around_y(first + (golden_angle * static_cast<float>(k)) + (0.2f * azimuth_draw));
        stalk.base        = glm::vec3{0.0f, 0.045f * height, 0.0f} + ((0.03f * height) * stalk.out);
        stalk.length      = 0.68f * height * grown;
        stalk.elevation_0 = glm::radians(60.0f + (24.0f * rank) + (5.0f * elevation_draw));
        stalk.elevation_1 = stalk.elevation_0 - glm::radians(22.0f + (30.0f * plant.droop * (1.0f - (0.5f * rank))));
        stalk.power       = 1.5f;
        fit_leaf_arc(stalk, plant.floor_y + (0.3f * blade_length));
        add_stalk(builder, stalk, 0.011f * height);
        const float hang = glm::radians(38.0f + (28.0f * plant.droop) + (8.0f * hang_draw));
        add_heart(builder, Heart{
            .hub       = arc_frame(stalk, 1.0f).center + ((0.03f * blade_length) * y_axis),
            .direction = (std::cos(hang) * stalk.out) - (std::sin(hang) * y_axis),
            .normal    = (std::sin(hang) * stalk.out) + (std::cos(hang) * y_axis),
            .length    = blade_length,
            .width     = plant.leaf_width / 0.35f,
            .thickness = 0.012f * blade_length,
            .floor_y   = plant.floor_y
        });
    }
    add_rolled_leaf(builder, random, glm::vec3{0.0f, 0.045f * height, 0.0f}, 0.55f * height, 0.03f * height);
}

// Strelitzia: paddle leaves on long stalks standing in one fan, alternately to either side.
void add_strelitzia(Kit_mesh_builder& builder, Kit_random& random, const Leaf_plant& plant)
{
    const float height = plant.height;
    builder.set_slot(plant_bark_slot);
    static_cast<void>(add_stem(builder, random, plant_mound, 6, 0.08f * height, 0.08f * height, glm::vec3{0.0f}, 0.0f));
    const float fan_heading = random.next_range(0.0f, two_pi);
    builder.set_slot(plant_foliage_slot);
    for (int k = 0; k < plant.leaves; ++k) {
        const float rank         = leaf_rank(k, plant.leaves);
        const float azimuth_draw = next_signed(random);
        const float lean_draw    = next_signed(random);
        const float length_draw  = random.next_unit();
        const float twist_draw   = next_signed(random);
        const float side_sign    = ((k % 2) == 0) ? 1.0f : -1.0f;
        const float lean         = glm::radians(8.0f + (36.0f * (1.0f - rank)) + (4.0f * lean_draw));
        Blade blade;
        blade.arc.out         = side_sign * around_y(fan_heading + (0.2f * azimuth_draw));
        blade.arc.base        = glm::vec3{0.0f, 0.05f * height, 0.0f} + ((0.012f * height * static_cast<float>(1 + (k / 2))) * blade.arc.out);
        blade.arc.length      = 0.95f * height * (0.72f + (0.28f * rank)) * (0.9f + (0.1f * length_draw));
        blade.arc.elevation_0 = (0.5f * pi) - (0.5f * lean);
        blade.arc.elevation_1 = (0.5f * pi) - (lean * (1.0f + (1.4f * plant.droop)));
        blade.arc.power       = 1.3f;
        blade.arc.twist       = 0.5f * twist_draw;
        blade.half_width      = 0.5f * plant.leaf_width * (0.48f * blade.arc.length);
        blade.thickness       = 0.011f * blade.arc.length;
        blade.stalk           = 0.52f;
        blade.stalk_width     = 0.16f;
        blade.stalk_stations  = 2;
        blade.blade_stations  = 5;
        blade.widest_from     = 0.3f;
        blade.widest_to       = 0.62f;
        blade.fold_base       = 0.55f;
        blade.fold_tip        = 0.05f;
        fit_leaf_arc(blade.arc, plant.floor_y + blade.half_width);
        add_blade(builder, blade);
    }
    add_rolled_leaf(builder, random, glm::vec3{0.0f, 0.05f * height, 0.0f}, 0.6f * height, 0.028f * height);
}

// The fronds of a fern's crown at the base point, and its croziers.
void add_fern_crown(Kit_mesh_builder& builder, Kit_random& random, Kit_random& cells, const Leaf_plant& plant, const glm::vec3& crown, const float frond_length, const float low_elevation)
{
    const float height = plant.height;
    const float first  = random.next_range(0.0f, two_pi);
    const int   pairs  = (plant.leaves > 10) ? 7 : 9;
    builder.set_slot(plant_foliage_slot);
    for (int k = 0; k < plant.leaves; ++k) {
        const float rank           = leaf_rank(k, plant.leaves);
        const float azimuth_draw   = next_signed(random);
        const float elevation_draw = next_signed(random);
        const float length_draw    = random.next_unit();
        Frond frond;
        frond.arc.out         = around_y(first + (golden_angle * static_cast<float>(k)) + (0.2f * azimuth_draw));
        frond.arc.base        = crown + ((0.03f * height) * frond.arc.out);
        frond.arc.length      = frond_length * (0.78f + (0.22f * length_draw)) * (1.0f - (0.3f * rank));
        frond.arc.elevation_0 = glm::radians(low_elevation + (44.0f * rank) + (6.0f * elevation_draw));
        frond.arc.elevation_1 = frond.arc.elevation_0 - glm::radians(50.0f + (75.0f * plant.droop * (1.2f - (0.5f * rank))));
        frond.arc.power       = 1.4f;
        frond.outline         = Frond_outline::fern;
        frond.pairs           = pairs;
        frond.rachis_width    = 0.009f * height;
        frond.leaflet_length  = 0.8f * plant.leaf_width * frond.arc.length;
        frond.leaflet_width   = 0.8f;
        frond.rise            = glm::radians(8.0f);
        frond.sweep           = glm::radians(16.0f);
        frond.sag             = 0.12f;
        frond.first           = 0.12f;
        fit_leaf_arc(frond.arc, plant.floor_y + (0.3f * frond.leaflet_length));
        add_frond(builder, frond);
    }
    builder.set_slot(plant_shoot_slot);
    const int   crozier_count = 2 + static_cast<int>(cells.next_u32() % 2u);
    const float crozier_first = random.next_range(0.0f, two_pi);
    for (int k = 0; k < crozier_count; ++k) {
        const float azimuth_draw = next_signed(random);
        const float length_draw  = random.next_unit();
        const float curl_draw    = random.next_unit();
        Leaf_arc arc;
        arc.out         = around_y(crozier_first + (two_pi * (static_cast<float>(k) + (0.25f * azimuth_draw)) / static_cast<float>(crozier_count)));
        arc.base        = crown + ((0.012f * height) * arc.out);
        arc.length      = 0.45f * frond_length * (0.7f + (0.3f * length_draw));
        arc.elevation_0 = glm::radians(80.0f);
        arc.elevation_1 = arc.elevation_0 + glm::radians(330.0f + (60.0f * curl_draw));
        arc.power       = 2.4f;
        add_crozier(builder, arc, 0.016f * height);
    }
}

void add_fern(Kit_mesh_builder& builder, Kit_random& random, Kit_random& cells, const Leaf_plant& plant)
{
    const float height = plant.height;
    builder.set_slot(plant_bark_slot);
    static_cast<void>(add_stem(builder, random, plant_mound, 6, 0.07f * height, 0.06f * height, glm::vec3{0.0f}, 0.0f));
    add_fern_crown(builder, random, cells, plant, glm::vec3{0.0f, 0.045f * height, 0.0f}, 0.95f * height, 36.0f);
}

// Tree fern: a fern's crown on a fibrous trunk, with dead fronds hanging under it.
void add_tree_fern(Kit_mesh_builder& builder, Kit_random& random, Kit_random& cells, const Leaf_plant& plant)
{
    const float     height       = plant.height;
    const float     trunk_height = 0.62f * height;
    const float     lean_heading = random.next_range(0.0f, two_pi);
    const float     lean_draw    = random.next_range(0.0f, 0.08f);
    builder.set_slot(plant_bark_slot);
    const glm::vec3 crown = add_stem(builder, random, plant_fern_trunk, 7, 0.045f * height, trunk_height, (lean_draw * trunk_height) * around_y(lean_heading), 0.05f);
    const int dead_count = 2 + static_cast<int>(cells.next_u32() % 2u);
    const float dead_first = random.next_range(0.0f, two_pi);
    for (int k = 0; k < dead_count; ++k) {
        const float azimuth_draw = next_signed(random);
        Frond frond;
        frond.arc.out         = around_y(dead_first + (two_pi * (static_cast<float>(k) + (0.25f * azimuth_draw)) / static_cast<float>(dead_count)));
        frond.arc.base        = crown - glm::vec3{0.0f, 0.06f * height, 0.0f} + ((0.04f * height) * frond.arc.out);
        frond.arc.length      = 0.36f * height;
        frond.arc.elevation_0 = glm::radians(-45.0f);
        frond.arc.elevation_1 = glm::radians(-84.0f);
        frond.outline         = Frond_outline::fern;
        frond.pairs           = 5;
        frond.rachis_width    = 0.008f * height;
        frond.leaflet_length  = 0.07f * height;
        frond.leaflet_width   = 0.7f;
        frond.rise            = glm::radians(-15.0f);
        frond.sweep           = glm::radians(25.0f);
        frond.sag             = 0.3f;
        add_frond(builder, frond);
    }
    add_fern_crown(builder, random, cells, plant, crown - glm::vec3{0.0f, 0.02f * height, 0.0f}, 0.66f * height, 8.0f);
}

void generate_leaf_plant(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form = values.get_enum("form");
    const Leaf_plant plant{
        .height     = values.get_float("height"),
        .leaves     = values.get_int("leaves"),
        .droop      = values.get_float("droop"),
        .leaf_width = values.get_float("leaf_width"),
        .floor_y    = 0.03f * values.get_float("height")
    };
    Kit_random random{values.get_seed(), "leaf plant v2"};       // moves vertices
    Kit_random cells {values.get_seed(), "leaf plant v2 cells"}; // decides the topology: integer draws only
    Kit_mesh_builder builder;
    if (form == "taro") {
        add_taro(builder, random, plant);
    } else if (form == "strelitzia") {
        add_strelitzia(builder, random, plant);
    } else if (form == "fern") {
        add_fern(builder, random, cells, plant);
    } else if (form == "tree_fern") {
        add_tree_fern(builder, random, cells, plant);
    } else {
        add_banana(builder, random, cells, plant);
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_nature_plant_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_parameter bend = derive_float(make_ratio_parameter("bend", "Horizontal offset of the crown, a share of the height", 0.15f), 0.0f, 0.35f, 0.01f);
        bend.float_max = 0.5f;
        Kit_parameter droop = derive_float(make_ratio_parameter("droop", "How far the fronds hang down", 0.4f), 0.2f, 0.7f, 0.01f);

        Kit_shape_definition shape;
        shape.id              = "nature.palm";
        shape.foliage_radius  = 2.0f;
        shape.kit             = "nature";
        shape.display_name    = "Palm";
        shape.description     =
            "Single-trunk palm standing at its origin; the species sets the trunk line and the crown: coconut (curved slender "
            "trunk, feather fronds, nuts), date (thick ringed trunk, stiff fronds), royal (straight column with a green "
            "crownshaft), fan (palmate leaves and a skirt of dead ones) or bottle (swollen trunk with a crownshaft).";
        shape.tags            = {"obstacle", "decoration", "tropical", "size:4x4x6"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("species", "Growth form: trunk line and crown", {"coconut", "date", "royal", "fan", "bottle"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the crown above the base", 2.0f, Kit_default_scale::wall_heights, 1.5f, 40.0f), 1.0f, 5.0f, 0.1f)),
            from_seed_by_default(bend),
            from_seed_by_default(derive_int(make_count_parameter("fronds", "Fronds or fan leaves in the crown", 14, 3, 24), 8, 20)),
            derive_float(make_length_parameter("frond_length", "Length of a frond", 4.0f, Kit_default_scale::meters, 0.3f, 8.0f), 3.0f, 5.5f, 0.1f),
            from_seed_by_default(droop),
            make_seed_parameter  (),
            make_count_parameter ("segments", "Trunk sides", 6, 4, 12)
        };
        shape.material_slots  = {"bark", "leaf"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 1600;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_palm;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.leaf_plant";
        shape.foliage_radius  = 0.6f;
        shape.former_ids      = {"nature.leaf_plant.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Leaf plant";
        shape.description     =
            "Lush plant of large leaves standing at its origin: banana (broad oblong leaves arching from a stout stem, dry "
            "ones hanging down it), taro (heart-shaped blades hanging from long stalks), strelitzia (paddle leaves on "
            "stalks in one fan), fern (arching fronds of leaflets about unrolling croziers) or tree fern (a fern's crown "
            "on a trunk over a skirt of dead fronds). The old leaves are low and droop, the young ones stand; the youngest "
            "are slot shoot, stems and dead leaves slot bark. No body, a walker passes through it.";
        shape.tags            = {"decoration", "tropical", "size:2x2x2"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Growth form", {"banana", "taro", "strelitzia", "fern", "tree_fern"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the plant", 1.8f, Kit_default_scale::meters, 0.3f, 8.0f), 0.4f, 4.0f, 0.1f)),
            from_seed_by_default(derive_int(make_count_parameter("leaves", "Leaves or fronds", 8, 3, 14), 4, 12)),
            from_seed_by_default(derive_float(make_ratio_parameter("droop", "How far the leaves hang down", 0.4f), 0.1f, 0.7f, 0.01f)),
            derive_float(make_ratio_parameter("leaf_width", "Width of a leaf, a share of its length", 0.35f), 0.25f, 0.45f, 0.01f),
            make_seed_parameter()
        };
        shape.parameters[4].float_min = 0.1f;
        shape.parameters[4].float_max = 0.6f;
        shape.material_slots  = {"bark", "leaf", "young_leaf"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 520;
        shape.collision       = Kit_collision::none;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_leaf_plant;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.palm_clump";
        shape.foliage_radius  = 1.0f;
        shape.kit             = "nature";
        shape.display_name    = "Palm clump";
        shape.description     =
            "Several slender palm stems rising from one root ball at its origin, the tallest in the middle and the others "
            "leaning out, each with a small crown: areca (ringed canes with arching feather fronds), rhapis "
            "(thin canes with fan leaves) or nipa (long erect fronds from short stems at the ground).";
        shape.tags            = {"obstacle", "decoration", "tropical", "size:2x2x4"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("species", "Growth form of the stems and crowns", {"areca", "rhapis", "nipa"}, 0)),
            from_seed_by_default(derive_int(make_count_parameter("stems", "Stems from the root", 5, 1, 9), 3, 9)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the tallest stem's crown", 1.2f, Kit_default_scale::wall_heights, 0.5f, 16.0f), 0.6f, 1.6f, 0.1f)),
            derive_float(make_ratio_parameter("spread", "How far the outer stems lean out", 0.5f), 0.2f, 0.8f, 0.05f),
            derive_int(make_count_parameter("fronds", "Fronds or fan leaves per stem", 6, 2, 10), 4, 8),
            make_seed_parameter()
        };
        shape.material_slots  = {"bark", "leaf"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 1940;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_palm_clump;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.palm_shrub";
        shape.foliage_radius  = 0.6f;
        shape.former_ids      = {"nature.palm_shrub.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Palm shrub";
        shape.description     =
            "Trunkless palm crown at the ground at its origin: palmetto (pleated fans of pointed fingers on long stalks, "
            "the old ones low, dead ones on the ground), cycad (a rosette of arching feather fronds on a scaly barrel "
            "about a flush of new ones) or seedling (a few pleated undivided leaves standing on a thin stem). The "
            "youngest leaves are slot shoot, the stem and dead leaves slot bark.";
        shape.tags            = {"obstacle", "decoration", "tropical", "size:1x1x1"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("species", "Growth form of the crown", {"palmetto", "cycad", "seedling"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("size", "Size of the crown", 1.2f, Kit_default_scale::meters, 0.3f, 6.0f), 0.6f, 2.5f, 0.1f)),
            from_seed_by_default(derive_int(make_count_parameter("fronds", "Fronds or fan leaves", 10, 3, 16), 5, 16)),
            make_seed_parameter()
        };
        shape.material_slots  = {"bark", "leaf", "young_leaf"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 1300;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_palm_shrub;
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
