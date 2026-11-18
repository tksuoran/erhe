#include "level/kit_leaf.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/shapes/sweep.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
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

[[nodiscard]] auto get_shrub_habit(const std::string_view species) -> Palm_habit
{
    if (species == "cycad") {
        return Palm_habit{Palm_species{0.45f, 0.0f, 0.15f, 3, {{0.0f, 1.15f}, {0.5f, 1.0f}, {1.0f, 0.85f}}, 0.0f, Palm_crown::feather, 10.0f, 50.0f, 0.3f, 0.16f, 0.25f, 0}, 0.22f, 0.6f, 0.02f};
    }
    if (species == "seedling") {
        return Palm_habit{Palm_species{0.12f, 0.0f, 0.0f, 2, {{0.0f, 1.2f}, {1.0f, 0.8f}}, 0.0f, Palm_crown::feather, 40.0f, 80.0f, 0.2f, 0.3f, 0.15f, 0}, 0.15f, 0.65f, 0.01f};
    }
    return Palm_habit{Palm_species{0.5f, 0.0f, 0.05f, 2, {{0.0f, 1.0f}, {1.0f, 0.9f}}, 0.0f, Palm_crown::fan, 45.0f, 85.0f, 0.3f, 0.0f, 0.0f, 0}, 0.08f, 1.0f, 0.02f}; // palmetto
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

void generate_palm_shrub(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Palm_habit habit = get_shrub_habit(values.get_enum("species"));
    const float      size  = values.get_float("size");
    Kit_random random{values.get_seed(), "palm shrub"};
    Palm_tree tree;
    tree.height       = habit.trunk * size;
    tree.bend         = 0.0f;
    tree.heading      = 0.0f;
    tree.fronds       = values.get_int("fronds");
    tree.frond_length = habit.frond_length * size;
    tree.droop        = habit.species.droop_factor * random.next_range(0.5f, 1.0f);
    tree.sides        = 6;
    tree.frond_steps  = 4;
    tree.fan_sectors  = 7;
    tree.skirt        = false;
    tree.lowest       = palm_lowest(habit.species, tree.frond_length);
    tree.least_radius = habit.least_radius;
    Kit_mesh_builder builder;
    GEO::Mesh        scratch{3, true};
    add_palm_tree(builder, scratch, habit.species, tree, random);
    builder.write(mesh);
}

enum class Leaf_arrangement : unsigned int {
    spiral = 0, // around the crown at the golden angle
    fan         // in one vertical plane, alternating sides (strelitzia)
};

// What a leaf plant's form sets (doc/editor/level_kit_shapes.md "Leaf plant"); lengths are shares of the height.
class Leaf_form
{
public:
    float            trunk;          // 0: none, the leaves rise from a stump
    float            trunk_radius;
    float            petiole;
    float            blade;
    float            width_factor;   // times the leaf_width share of the blade's length
    float            elevation_low;  // the oldest leaf's petiole above the horizontal, degrees
    float            elevation_high; // the youngest's
    float            blade_turn;     // the blade's direction from the petiole's, degrees; negative hangs it down
    float            droop_factor;
    float            fold;
    float            widest_at;
    float            widest_end;
    float            base_width;
    float            roll;           // the blade's turn about its midrib, degrees: 90 holds it in the plane of its stalk
    Leaf_arrangement arrangement;
};

[[nodiscard]] auto get_leaf_form(const std::string_view form) -> Leaf_form
{
    if (form == "taro") {
        return Leaf_form{0.0f, 0.0f, 0.55f, 0.5f, 2.0f, 50.0f, 78.0f, -90.0f, 0.5f, 0.12f, 0.25f, 0.4f, 0.6f, 0.0f, Leaf_arrangement::spiral};
    }
    if (form == "strelitzia") {
        return Leaf_form{0.0f, 0.0f, 0.5f, 0.45f, 0.9f, 55.0f, 85.0f, -12.0f, 0.4f, 0.15f, 0.3f, 0.7f, 0.2f, 90.0f, Leaf_arrangement::fan};
    }
    if (form == "fern") {
        return Leaf_form{0.0f, 0.0f, 0.05f, 0.95f, 0.55f, 35.0f, 70.0f, 0.0f, 1.2f, 0.55f, 0.35f, 0.0f, 0.15f, 0.0f, Leaf_arrangement::spiral};
    }
    if (form == "tree_fern") {
        return Leaf_form{0.62f, 0.035f, 0.04f, 0.55f, 0.55f, 5.0f, 45.0f, 0.0f, 1.0f, 0.55f, 0.35f, 0.0f, 0.15f, 0.0f, Leaf_arrangement::spiral};
    }
    return Leaf_form{0.45f, 0.045f, 0.06f, 0.6f, 1.0f, 15.0f, 70.0f, 0.0f, 0.9f, 0.1f, 0.2f, 0.75f, 0.25f, 0.0f, Leaf_arrangement::spiral}; // banana
}

void generate_leaf_plant(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Leaf_form form       = get_leaf_form(values.get_enum("form"));
    const float     height     = values.get_float("height");
    const int       leaf_count = values.get_int("leaves");
    const float     droop      = values.get_float("droop") * form.droop_factor;
    const float     leaf_width = values.get_float("leaf_width") * form.width_factor;
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    Kit_mesh_builder builder;
    GEO::Mesh        scratch{3, true};
    builder.set_slot(palm_bark_slot);

    // A trunk leaning toward a seeded heading, or a stump the leaves rise from.
    Kit_random      trunk_random{values.get_seed(), "leaf plant trunk"};
    const float     heading = trunk_random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    const glm::vec3 lean{std::cos(heading), 0.0f, -std::sin(heading)};
    glm::vec3       crown{0.0f};
    float           crown_radius = std::max(0.03f, 0.05f * height);
    if (form.trunk > 0.0f) {
        const float trunk_height = form.trunk * height;
        const float bend         = trunk_random.next_range(0.0f, 0.08f) * trunk_height;
        const float radius       = std::max(0.04f, form.trunk_radius * height);
        erhe::geometry::shapes::Sweep_parameters trunk;
        trunk.profile     = regular_profile(6, radius);
        trunk.spine       = {glm::vec3{0.0f}, glm::vec3{0.0f, trunk_height / 3.0f, 0.0f}, (0.3f * bend * lean) + glm::vec3{0.0f, 0.7f * trunk_height, 0.0f}, (bend * lean) + glm::vec3{0.0f, trunk_height, 0.0f}};
        trunk.spine_steps = 6;
        trunk.taper       = {{0.0f, 1.25f}, {0.15f, 1.0f}, {1.0f, 0.8f}};
        add_kit_sweep(builder, trunk, scratch);
        crown        = trunk.spine.back();
        crown_radius = 0.8f * radius;
    } else {
        const float stump = std::max(0.02f, 0.06f * height);
        builder.add_prism(6, crown_radius, 0.0f, stump, 0.0f);
        crown = glm::vec3{0.0f, 0.8f * stump, 0.0f};
    }

    Kit_random  leaf_random{values.get_seed(), "leaf plant leaves"};
    const float first_heading = leaf_random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    builder.set_slot(palm_foliage_slot);
    for (int k = 0; k < leaf_count; ++k) {
        // Rank 1 is the youngest leaf at the center: it stands up and droops least.
        const float rank  = (leaf_count > 1) ? (static_cast<float>(k) / static_cast<float>(leaf_count - 1)) : 1.0f;
        const float angle = (form.arrangement == Leaf_arrangement::fan)
            ? (first_heading + (((k % 2) == 0) ? 0.0f : std::numbers::pi_v<float>) + leaf_random.next_range(-0.25f, 0.25f))
            : (first_heading + (palm_golden_angle * static_cast<float>(k)) + leaf_random.next_range(-0.2f, 0.2f));
        const glm::vec3 outward{std::cos(angle), 0.0f, -std::sin(angle)};
        const float     size      = (0.65f + (0.45f * leaf_random.next_unit())) * (1.0f - (0.25f * rank));
        const float     elevation = glm::radians(form.elevation_low + ((form.elevation_high - form.elevation_low) * rank) + leaf_random.next_range(-8.0f, 8.0f));
        const glm::vec3 direction = (std::cos(elevation) * outward) + (std::sin(elevation) * up);
        const float     petiole   = form.petiole * height * size;
        const float     blade     = form.blade * height * size;
        const float     width     = leaf_width * blade;
        const float     thickness = std::max(0.004f, 0.01f * blade);
        // Nothing reaches lower than this: a hanging or drooping leaf rests just above the ground.
        const float     lowest    = 0.02f + thickness + (0.5f * width * std::max(std::abs(std::sin(glm::radians(form.roll))), form.fold));
        const auto      lift      = [lowest](glm::vec3 p) -> glm::vec3 {
            p.y = std::max(p.y, lowest);
            return p;
        };

        const glm::vec3 base = crown + ((0.6f * crown_radius) * outward);
        const glm::vec3 tip  = lift(base + (petiole * direction));
        Kit_leaf stalk;
        stalk.spine      = {base, base + ((petiole / 3.0f) * direction), lift(base + ((2.0f * petiole / 3.0f) * direction)), tip};
        stalk.section    = Kit_leaf_section::thick;
        stalk.width      = std::max(0.012f, 0.035f * blade);
        stalk.widest_at  = 0.1f;
        stalk.base_width = 1.0f;
        stalk.steps      = 2;
        add_leaf(builder, stalk, scratch);

        const float     turn      = elevation + glm::radians(form.blade_turn);
        const glm::vec3 heading_b = (std::cos(turn) * outward) + (std::sin(turn) * up);
        const float     sag       = std::clamp(droop * (1.2f - (0.7f * rank)), 0.0f, 1.0f);
        Kit_leaf leaf;
        leaf.spine = {
            tip,
            lift(tip + ((0.35f * blade) * heading_b)),
            lift(tip + ((0.7f * blade) * heading_b) - ((0.3f * sag * blade) * up)),
            lift(tip + ((0.9f * blade * (1.0f - (0.2f * sag))) * heading_b) - ((0.75f * sag * blade) * up))
        };
        leaf.section    = Kit_leaf_section::folded;
        leaf.width      = width;
        leaf.thickness  = thickness;
        leaf.fold       = form.fold;
        leaf.widest_at  = form.widest_at;
        leaf.widest_end = form.widest_end;
        leaf.roll       = glm::radians(form.roll);
        leaf.base_width = form.base_width;
        leaf.steps      = 6;
        add_leaf(builder, leaf, scratch);
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
        shape.material_slots  = {"bark", "foliage"};
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
        shape.kit             = "nature";
        shape.display_name    = "Leaf plant";
        shape.description     =
            "Rosette of large leaves on stalks standing at its origin: banana (broad oblong leaves on a trunk), taro "
            "(heart-shaped leaves hanging from long stalks), strelitzia (paddle leaves in a fan), fern (arching fronds) or "
            "tree fern (fronds on a trunk); no body, a walker passes through it.";
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
        shape.material_slots  = {"bark", "foliage"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 640;
        shape.collision       = Kit_collision::none;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_leaf_plant;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.palm_clump";
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
        shape.material_slots  = {"bark", "foliage"};
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
        shape.kit             = "nature";
        shape.display_name    = "Palm shrub";
        shape.description     =
            "Trunkless palm crown at the ground at its origin: palmetto (fan leaves on long stalks), cycad (a rosette of "
            "stiff feather fronds on a short rough stump) or seedling (a few broad upright fronds on a thin stem).";
        shape.tags            = {"obstacle", "decoration", "tropical", "size:1x1x1"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("species", "Growth form of the crown", {"palmetto", "cycad", "seedling"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("size", "Size of the crown", 1.2f, Kit_default_scale::meters, 0.3f, 6.0f), 0.6f, 2.5f, 0.1f)),
            from_seed_by_default(derive_int(make_count_parameter("fronds", "Fronds or fan leaves", 10, 3, 16), 5, 16)),
            make_seed_parameter()
        };
        shape.material_slots  = {"bark", "foliage"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 600;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_palm_shrub;
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
