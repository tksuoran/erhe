#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace editor {

namespace {

auto to_degrees(const float radians) -> float
{
    return radians * (180.0f / std::numbers::pi_v<float>);
}

auto check_walk_width(const std::string_view id, const char* name, const float width, const Kit_agent& agent) -> std::string
{
    const float min_width = (2.0f * agent.radius) + kit_opening_margin;
    if (width < min_width) {
        return fmt::format("{}: '{}' {} m is narrower than the agent's {} m", id, name, width, min_width);
    }
    return {};
}

auto check_walk_width(const Kit_shape_definition& shape, const char* name, const float width, const Kit_agent& agent) -> std::string
{
    return check_walk_width(std::string_view{shape.id}, name, width, agent);
}

// A box on y = 0 around the Y axis; corner k has x at bit 0, y at bit 1 and z at bit 2.
void add_base_box(Kit_mesh_builder& builder, const float half_x, const float height, const float half_z)
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

void generate_block(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    Kit_mesh_builder builder;
    add_base_box(builder, 0.5f * values.get_float("width"), values.get_float("height"), 0.5f * values.get_float("depth"));
    builder.write(mesh);
}

void generate_floor_slab(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    Kit_mesh_builder builder;
    add_base_box(builder, 0.5f * values.get_float("width"), values.get_float("thickness"), 0.5f * values.get_float("depth"));
    builder.write(mesh);
}

void generate_wedge(const Kit_parameter_values& values, GEO::Mesh& mesh)
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

void generate_cylinder(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const int   segments = values.get_int("segments");
    const float radius   = values.get_float("radius");
    const float height   = values.get_float("height");
    Kit_mesh_builder builder;
    // Each side edge is one (x, z) used at both ends, so the sides are exactly vertical.
    std::vector<uint32_t> bottom_ring;
    std::vector<uint32_t> top_ring;
    for (int i = 0; i < segments; ++i) {
        const glm::vec2 point = radius * get_kit_circle_point(i, segments);
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
class Stairs_side
{
public:
    std::vector<uint32_t> base;  // on y = 0
    std::vector<uint32_t> inner; // the foot of riser i, the back end of tread i - 1; inner[0] is base[0]
    std::vector<uint32_t> nose;  // the top of riser i, the front end of tread i
};

enum class Stairs_side_facing : unsigned int { minus_x = 0, plus_x };

// The side wall: a column per step, a trapezoid up to the line of the riser feet and the step's triangle above it.
void add_stairs_side_facets(Kit_mesh_builder& builder, const Stairs_side& side, const Stairs_side_facing facing)
{
    const bool        plus_x = (facing == Stairs_side_facing::plus_x);
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

void generate_stairs(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Kit_stairs_layout layout = get_stairs_layout(values);
    const int   steps    = layout.step_count;
    const float rise     = values.get_float("rise");
    const float half_x   = 0.5f * values.get_float("width");
    const float half_run = 0.5f * layout.run;

    Kit_mesh_builder builder;
    std::array<Stairs_side, 2> sides; // -x, +x
    for (std::size_t s = 0; s < 2; ++s) {
        const float     x    = (s == 0) ? -half_x : half_x;
        Stairs_side& side = sides[s];
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
    const Stairs_side& left  = sides[0];
    const Stairs_side& right = sides[1];
    const std::size_t     count = static_cast<std::size_t>(steps);
    for (std::size_t i = 0; i < count; ++i) {
        builder.add_quad(left.inner[i], right.inner[i], right.nose[i], left.nose[i]);          // riser
        builder.add_quad(left.nose[i], right.nose[i], right.inner[i + 1], left.inner[i + 1]);  // tread
    }
    for (std::size_t i = 0; i < count; ++i) {
        builder.add_quad(left.base[i], left.base[i + 1], right.base[i + 1], right.base[i]);    // -y, a strip per step
    }
    builder.add_quad(left.base[count], left.inner[count], right.inner[count], right.base[count]); // -z
    add_stairs_side_facets(builder, left,  Stairs_side_facing::minus_x);
    add_stairs_side_facets(builder, right, Stairs_side_facing::plus_x);
    builder.write(mesh);
}

// Front (+Z), back, left (-X) and right side connectors at the centers of the box sides' edges at height y.
void add_box_side_connectors(std::vector<Kit_connector>& out_connectors, const float half_x, const float half_z, const float y)
{
    add_side_connector(out_connectors, "front", glm::vec3{0.0f, y,  half_z}, glm::vec3{ 0.0f, 0.0f,  1.0f});
    add_side_connector(out_connectors, "back",  glm::vec3{0.0f, y, -half_z}, glm::vec3{ 0.0f, 0.0f, -1.0f});
    add_side_connector(out_connectors, "left",  glm::vec3{-half_x, y, 0.0f}, glm::vec3{-1.0f, 0.0f,  0.0f});
    add_side_connector(out_connectors, "right", glm::vec3{ half_x, y, 0.0f}, glm::vec3{ 1.0f, 0.0f,  0.0f});
}

// The foot at the front edge of the base and the top at the back edge of the top, then the base (stairs, wedge).
void add_climb_connectors(std::vector<Kit_connector>& out_connectors, const float run, const float rise)
{
    add_side_connector(out_connectors, "bottom", glm::vec3{0.0f, 0.0f,  0.5f * run}, glm::vec3{0.0f, 0.0f,  1.0f});
    add_side_connector(out_connectors, "top",    glm::vec3{0.0f, rise, -0.5f * run}, glm::vec3{0.0f, 0.0f, -1.0f});
    out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
}

} // anonymous namespace

auto get_kit_circle_point(const int i, const int n) -> glm::vec2
{
    if ((2 * i) > n) {
        const glm::vec2 mirrored = get_kit_circle_point(n - i, n);
        return glm::vec2{mirrored.x, -mirrored.y};
    }
    if (((n % 2) == 0) && ((4 * i) > n)) {
        const glm::vec2 mirrored = get_kit_circle_point((n / 2) - i, n);
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

void add_base_and_top_connectors(std::vector<Kit_connector>& out_connectors, const float top_y)
{
    out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f},              .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
    out_connectors.push_back(Kit_connector{.name = "top",  .position = glm::vec3{0.0f, top_y, 0.0f}, .outward = glm::vec3{0.0f,  1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
}

void type_wall_run_connectors(std::vector<Kit_connector>& out_connectors)
{
    for (Kit_connector& connector : out_connectors) {
        const std::string_view name{connector.name};
        if ((name == "start") || (name == "end")) {
            // Generic too, so a wall still meets a tower's or a pillar's side (orchestrator choice, C1.5).
            connector.types = Kit_connector_types{.bits = Kit_connector_types::of(Kit_connector_type::wall).bits | Kit_connector_types::of(Kit_connector_type::generic).bits};
        } else if (name == "base") {
            connector.types = Kit_connector_types::of(Kit_connector_type::wall_floor);
        } else if (name == "top") {
            // C10.2: riders stand on a wall's top; generic too, so a wall top still takes what it took before.
            connector.types    = Kit_connector_types{.bits = Kit_connector_types::of(Kit_connector_type::rail).bits | Kit_connector_types::of(Kit_connector_type::generic).bits};
            connector.rail_end = Kit_rail_end::host;
        }
    }
}

void type_rider_base_connector(std::vector<Kit_connector>& out_connectors)
{
    for (Kit_connector& connector : out_connectors) {
        if (std::string_view{connector.name} == "base") {
            connector.types    = Kit_connector_types{.bits = Kit_connector_types::of(Kit_connector_type::rail).bits | Kit_connector_types::of(Kit_connector_type::generic).bits};
            connector.rail_end = Kit_rail_end::rider;
        }
    }
}

auto get_kit_repeat_count(const float length, const float spacing, const int max_count) -> int
{
    if (!(length > 0.0f) || !(spacing > 0.0f) || !std::isfinite(length) || !std::isfinite(spacing) || (max_count < 1)) {
        return 1;
    }
    // The two counts around length / spacing; the one whose repeat is nearer the spacing, the smaller on a tie.
    const float ratio = std::min(length / spacing, static_cast<float>(max_count));
    const int   lower = std::clamp(static_cast<int>(std::floor(ratio)), 1, max_count);
    const int   upper = std::clamp(lower + 1, 1, max_count);
    const float lower_error = std::abs((length / static_cast<float>(lower)) - spacing);
    const float upper_error = std::abs((length / static_cast<float>(upper)) - spacing);
    return (upper_error < lower_error) ? upper : lower;
}

void set_wall_run_flow(Kit_shape_definition& shape, const char* const thickness_parameter)
{
    shape.property_mappings = {
        Kit_property_mapping{.property = Kit_standard_property::room_height,    .target = "height"},
        Kit_property_mapping{.property = Kit_standard_property::child_length,    .target = "length"},
        Kit_property_mapping{.property = Kit_standard_property::wall_material,  .target = "wall"}
    };
    if (thickness_parameter != nullptr) {
        shape.property_mappings.push_back(Kit_property_mapping{.property = Kit_standard_property::wall_thickness, .target = thickness_parameter});
    }
    std::size_t wall_slot = 0;
    for (std::size_t i = 0, end = shape.material_slots.size(); i < end; ++i) {
        if (shape.material_slots[i] == "wall") {
            wall_slot = i;
        }
    }
    shape.get_supply = [wall_slot, thickness_parameter](const Kit_piece_view& piece, const std::string_view name, Kit_supply_list& out_supply) {
        out_supply.clear();
        if (name == "top") {
            // C10.2: a rider on the top runs the wall's length.
            out_supply.add(Kit_supplied_value{.property = Kit_standard_property::child_length, .priority = c_wall_supply_priority, .length = piece.values->get_float("length")});
            if (thickness_parameter != nullptr) {
                out_supply.add(Kit_supplied_value{.property = Kit_standard_property::wall_thickness, .priority = c_wall_supply_priority, .length = piece.values->get_float(thickness_parameter)});
            }
            return;
        }
        if ((name != "start") && (name != "end")) {
            return;
        }
        if (thickness_parameter != nullptr) {
            out_supply.add(Kit_supplied_value{.property = Kit_standard_property::wall_thickness, .priority = c_wall_supply_priority, .length = piece.values->get_float(thickness_parameter)});
        }
        out_supply.add(Kit_supplied_value{.property = Kit_standard_property::room_height,    .priority = c_wall_supply_priority, .length = piece.values->get_float("height")});
        if (wall_slot < piece.slot_materials.size()) {
            out_supply.add(Kit_supplied_value{.property = Kit_standard_property::wall_material, .priority = c_wall_supply_priority, .material = piece.slot_materials[wall_slot]});
        }
    };
}

void add_side_connector(std::vector<Kit_connector>& out_connectors, const char* const name, const glm::vec3& position, const glm::vec3& outward)
{
    out_connectors.push_back(Kit_connector{.name = name, .position = position, .outward = outward, .up = glm::vec3{0.0f, 1.0f, 0.0f}});
}

auto get_stairs_layout(const Kit_parameter_values& values) -> Kit_stairs_layout
{
    const float rise  = values.get_float("rise");
    const float limit = values.get_float("step_rise_limit");
    Kit_stairs_layout layout{};
    if ((rise <= 0.0f) || (limit <= 0.0f)) {
        return layout;
    }
    // The small tolerance keeps rise 3.0 over limit 0.2 at 15 steps despite rounding.
    layout.step_count    = std::max(1, static_cast<int>(std::ceil((rise / limit) - 1.0e-4f)));
    layout.step_rise     = rise / static_cast<float>(layout.step_count);
    layout.tread_depth   = kit_stairs_tread_per_rise * layout.step_rise;
    layout.run           = layout.tread_depth * static_cast<float>(layout.step_count);
    layout.slope_degrees = to_degrees(std::atan2(layout.step_rise, layout.tread_depth));
    return layout;
}

auto check_stairs_walkability(const std::string_view name, const float step_rise, const float tread_depth, const float width, const Kit_agent& agent) -> std::string
{
    if (step_rise > agent.max_climb) {
        return fmt::format("{}: step rise {} m is higher than the agent's climb {} m", name, step_rise, agent.max_climb);
    }
    const float slope_degrees = to_degrees(std::atan2(step_rise, tread_depth));
    if (slope_degrees > agent.max_slope_degrees) {
        return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", name, slope_degrees, agent.max_slope_degrees);
    }
    return check_walk_width(name, "width", width, agent);
}

void add_basic_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "basic.block";
        shape.former_ids      = {"basic.block.v2"};
        shape.kit             = "basic";
        shape.display_name    = "Block";
        shape.description     = "Box with its origin at the center of its base; tiles on the module grid.";
        shape.tags            = {"obstacle", "walkable", "tiles", "size:1x1x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("width",  "Size along X", 1.0f, Kit_default_scale::modules, 0.05f, 64.0f), 0.5f, 3.0f, 0.25f),
            derive_float(make_length_parameter("height", "Size along Y", 1.0f, Kit_default_scale::modules, 0.05f, 64.0f), 0.5f, 3.0f, 0.25f),
            derive_float(make_length_parameter("depth",  "Size along Z", 1.0f, Kit_default_scale::modules, 0.05f, 64.0f), 0.5f, 3.0f, 0.25f),
            make_seed_parameter()
        };
        shape.material_slots  = {"stone"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 12;
        shape.collision       = Kit_collision::box;
        shape.generator       = generate_block;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return all_sides(); };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
            add_box_side_connectors(out_connectors, 0.5f * values.get_float("width"), 0.5f * values.get_float("depth"), 0.0f);
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "basic.wedge";
        shape.former_ids      = {"basic.wedge.v2"};
        shape.kit             = "basic";
        shape.display_name    = "Wedge / slope";
        shape.description     = "Ramp rising toward -Z from its front edge at +Z; origin at the center of its base.";
        shape.tags            = {"walkable", "slope", "size:1x1x2"};
        shape.parameters      = {
            // Derived slopes stay within 32 degrees (rise 1.25 over run 2 modules) and widths walkable (R2.10).
            derive_float(make_length_parameter("run",   "Horizontal length of the slope along Z",  2.0f, Kit_default_scale::modules, 0.1f,  64.0f), 2.0f,  4.0f,  0.25f),
            derive_float(make_length_parameter("rise",  "Height of the high end",                  1.0f, Kit_default_scale::modules, 0.05f, 32.0f), 0.25f, 1.25f, 0.25f),
            derive_float(make_length_parameter("width", "Size along X",                            1.0f, Kit_default_scale::modules, 0.1f,  64.0f), 1.0f,  3.0f,  0.25f),
            make_seed_parameter()
        };
        shape.material_slots  = {"stone"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 8;
        shape.generator       = generate_wedge;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return std::vector<Kit_side>{Kit_side::y_neg, Kit_side::z_neg}; };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Kit_shape_definition& self  = *values.get_shape();
            const float                 slope = to_degrees(std::atan2(values.get_float("rise"), values.get_float("run")));
            if (slope > agent.max_slope_degrees) {
                return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", self.id, slope, agent.max_slope_degrees);
            }
            return check_walk_width(self, "width", values.get_float("width"), agent);
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_climb_connectors(out_connectors, values.get_float("run"), values.get_float("rise"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "basic.cylinder";
        shape.former_ids      = {"basic.cylinder.v2"};
        shape.kit             = "basic";
        shape.display_name    = "Cylinder";
        shape.description     = "Upright cylinder around Y with its origin at the center of its base.";
        shape.tags            = {"obstacle", "walkable", "size:1x1x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("radius", "Radius",       0.5f, Kit_default_scale::modules, 0.025f, 32.0f), 0.25f, 1.5f, 0.05f),
            derive_float(make_length_parameter("height", "Size along Y", 1.0f, Kit_default_scale::modules, 0.05f,  64.0f), 0.5f,  3.0f, 0.25f),
            make_count_parameter ("segments", "Sides around the axis", 16, 3, 64),
            make_seed_parameter  ()
        };
        shape.material_slots          = {"stone"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget         = 60;
        shape.generator               = generate_cylinder;
        shape.mirror_planes           = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors          = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "basic.stairs";
        shape.former_ids      = {"basic.stairs.v2"};
        shape.kit             = "basic";
        shape.display_name    = "Straight stairs";
        shape.description     =
            "Straight run of equal steps rising toward -Z from the front edge at +Z; the step count is the least that keeps each "
            "step within the step rise limit and the tread depth is 1.5 times the step rise.";
        shape.tags            = {"walkable", "stairs", "size:1xNx1"};
        shape.parameters      = {
            // The step rise limit is never derived: it keeps the stairs walkable (R2.10).
            derive_float(make_length_parameter("rise",            "Total height climbed",        1.0f, Kit_default_scale::wall_heights, 0.1f,  16.0f), 0.25f, 1.5f, 0.05f),
            derive_float(make_length_parameter("width",           "Size along X",                1.0f, Kit_default_scale::modules,      0.3f,  32.0f), 1.0f,  3.0f, 0.25f),
            make_length_parameter("step_rise_limit", "Highest allowed single step", 0.2f, Kit_default_scale::meters,       0.05f, 1.0f),
            make_seed_parameter()
        };
        shape.material_slots  = {"stone"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 180;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_stairs;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return std::vector<Kit_side>{Kit_side::y_neg, Kit_side::z_neg}; };
        shape.check_combination = [](const Kit_parameter_values& values) -> std::string {
            const Kit_stairs_layout layout = get_stairs_layout(values);
            if (layout.step_count > kit_stairs_max_steps) {
                return fmt::format(
                    "{}: 'rise' {} m over 'step_rise_limit' {} m makes {} steps, more than {}",
                    values.get_shape()->id, values.get_float("rise"), values.get_float("step_rise_limit"), layout.step_count, kit_stairs_max_steps
                );
            }
            return {};
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Kit_stairs_layout layout = get_stairs_layout(values);
            return check_stairs_walkability(values.get_shape()->id, layout.step_rise, layout.tread_depth, values.get_float("width"), agent);
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_climb_connectors(out_connectors, get_stairs_layout(values).run, values.get_float("rise"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "basic.floor_slab";
        shape.former_ids      = {"basic.floor_slab.v2"};
        shape.kit             = "basic";
        shape.display_name    = "Floor slab";
        shape.description     = "Flat slab with its origin at the center of its base; its top is the walking surface.";
        shape.tags            = {"walkable", "floor", "tiles", "size:2x2"};
        shape.parameters      = {
            derive_float(make_length_parameter("width",     "Size along X",  2.0f, Kit_default_scale::modules, 0.1f,  64.0f), 1.0f, 4.0f, 0.5f),
            derive_float(make_length_parameter("depth",     "Size along Z",  2.0f, Kit_default_scale::modules, 0.1f,  64.0f), 1.0f, 4.0f, 0.5f),
            derive_float(make_length_parameter("thickness", "Size along Y",  0.2f, Kit_default_scale::meters,  0.01f, 4.0f), 0.1f, 0.4f, 0.05f),
            make_seed_parameter()
        };
        shape.material_slots  = {"floor"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 12;
        shape.collision       = Kit_collision::box;
        shape.generator       = generate_floor_slab;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return all_sides(); };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Kit_shape_definition& self = *values.get_shape();
            const float narrow = std::min(values.get_float("width"), values.get_float("depth"));
            return check_walk_width(self, "width", narrow, agent);
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float thickness = values.get_float("thickness");
            add_base_and_top_connectors(out_connectors, thickness);
            add_box_side_connectors(out_connectors, 0.5f * values.get_float("width"), 0.5f * values.get_float("depth"), thickness);
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
