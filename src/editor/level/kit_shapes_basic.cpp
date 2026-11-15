#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace editor {

namespace {

auto to_degrees(const float radians) -> float
{
    return radians * (180.0f / std::numbers::pi_v<float>);
}

auto check_walk_width(const Kit_shape_definition& shape, const char* name, const float width, const Kit_agent& agent) -> std::string
{
    const float min_width = (2.0f * agent.radius) + kit_opening_margin;
    if (width < min_width) {
        return fmt::format("{}: '{}' {} m is narrower than the agent's {} m", shape.id, name, width, min_width);
    }
    return {};
}

void generate_block(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float w = values.get_float("width");
    const float h = values.get_float("height");
    const float d = values.get_float("depth");
    Kit_mesh_builder builder;
    builder.add_box(glm::vec3{-0.5f * w, 0.0f, -0.5f * d}, glm::vec3{0.5f * w, h, 0.5f * d});
    builder.write(mesh);
}

void generate_wedge(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float run   = values.get_float("run");
    const float rise  = values.get_float("rise");
    const float width = values.get_float("width");
    const float x     = 0.5f * width;
    const float z     = 0.5f * run;
    Kit_mesh_builder builder;
    const uint32_t f0 = builder.add_vertex({-x, 0.0f,  z});
    const uint32_t f1 = builder.add_vertex({ x, 0.0f,  z});
    const uint32_t b0 = builder.add_vertex({-x, 0.0f, -z});
    const uint32_t b1 = builder.add_vertex({ x, 0.0f, -z});
    const uint32_t t0 = builder.add_vertex({-x, rise, -z});
    const uint32_t t1 = builder.add_vertex({ x, rise, -z});
    builder.add_quad    (f0, b0, b1, f1); // bottom
    builder.add_quad    (b0, t0, t1, b1); // back
    builder.add_quad    (f0, f1, t1, t0); // slope
    builder.add_triangle(f0, t0, b0);     // -x
    builder.add_triangle(f1, b1, t1);     // +x
    builder.write(mesh);
}

void generate_cylinder(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    Kit_mesh_builder builder;
    builder.add_prism(values.get_int("segments"), values.get_float("radius"), 0.0f, values.get_float("height"), 0.0f);
    builder.write(mesh);
}

void generate_stairs(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Kit_stairs_layout layout = get_stairs_layout(values);
    const int   n      = layout.step_count;
    const float rise   = values.get_float("rise");
    const float x      = 0.5f * values.get_float("width");
    const float half_r = 0.5f * layout.run;

    // Side profile (z, y): front bottom, then each riser top and tread end, then back bottom.
    std::vector<glm::vec2> profile;
    const auto z_at = [&](const int i) -> float { return half_r - (layout.run * static_cast<float>(i) / static_cast<float>(n)); };
    const auto y_at = [&](const int i) -> float { return rise * static_cast<float>(i) / static_cast<float>(n); };
    profile.push_back({z_at(0), 0.0f});
    for (int i = 0; i < n; ++i) {
        profile.push_back({z_at(i),     y_at(i + 1)});
        profile.push_back({z_at(i + 1), y_at(i + 1)});
    }
    profile.push_back({z_at(n), 0.0f});

    Kit_mesh_builder builder;
    const uint32_t count = static_cast<uint32_t>(profile.size());
    const uint32_t left  = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec2& p : profile) {
        builder.add_vertex({-x, p.y, p.x});
    }
    const uint32_t right = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec2& p : profile) {
        builder.add_vertex({x, p.y, p.x});
    }
    const uint32_t apex = count - 1; // back bottom corner; fans from it cover the non-convex side profile

    for (uint32_t k = 0; k + 2 < count; ++k) {
        builder.add_triangle(left + apex, left + k, left + k + 1);
        builder.add_triangle(right + apex, right + k + 1, right + k);
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(n); ++i) {
        const uint32_t riser_bottom = 2 * i;
        const uint32_t riser_top    = (2 * i) + 1;
        const uint32_t tread_end    = (2 * i) + 2;
        builder.add_quad(left + riser_bottom, right + riser_bottom, right + riser_top, left + riser_top);
        builder.add_quad(left + riser_top,    right + riser_top,    right + tread_end, left + tread_end);
    }
    const uint32_t top = count - 2;
    builder.add_quad(left + apex, left + top, right + top, right + apex); // back
    builder.add_quad(left + 0, left + apex, right + apex, right + 0);     // bottom
    builder.write(mesh);
}

void generate_floor_slab(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float w = values.get_float("width");
    const float d = values.get_float("depth");
    const float t = values.get_float("thickness");
    Kit_mesh_builder builder;
    builder.add_box(glm::vec3{-0.5f * w, 0.0f, -0.5f * d}, glm::vec3{0.5f * w, t, 0.5f * d});
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

void add_base_and_top_connectors(std::vector<Kit_connector>& out_connectors, const float top_y)
{
    out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f},              .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
    out_connectors.push_back(Kit_connector{.name = "top",  .position = glm::vec3{0.0f, top_y, 0.0f}, .outward = glm::vec3{0.0f,  1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
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

void add_basic_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "basic.block";
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
        shape.triangle_budget = 130;
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
            const Kit_shape_definition& self   = *values.get_shape();
            const Kit_stairs_layout     layout = get_stairs_layout(values);
            if (layout.step_rise > agent.max_climb) {
                return fmt::format("{}: step rise {} m is higher than the agent's climb {} m", self.id, layout.step_rise, agent.max_climb);
            }
            if (layout.slope_degrees > agent.max_slope_degrees) {
                return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", self.id, layout.slope_degrees, agent.max_slope_degrees);
            }
            return check_walk_width(self, "width", values.get_float("width"), agent);
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_climb_connectors(out_connectors, get_stairs_layout(values).run, values.get_float("rise"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "basic.floor_slab";
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
