#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace editor {

namespace {

void generate_wall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float l = values.get_float("length");
    const float h = values.get_float("height");
    const float t = values.get_float("thickness");
    Kit_mesh_builder builder;
    builder.add_box(glm::vec3{-0.5f * l, 0.0f, -0.5f * t}, glm::vec3{0.5f * l, h, 0.5f * t});
    builder.write(mesh);
}

void generate_door_wall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float l  = values.get_float("length");
    const float h  = values.get_float("height");
    const float t  = values.get_float("thickness");
    const float ow = values.get_float("opening_width");
    const float oh = values.get_float("opening_height");

    // Vertex grid: x columns, y rows, front (+Z) and back (-Z); the cell (1, 0) is the opening.
    const std::array<float, 4> xs{-0.5f * l, -0.5f * ow, 0.5f * ow, 0.5f * l};
    const std::array<float, 3> ys{0.0f, oh, h};
    const std::array<float, 2> zs{0.5f * t, -0.5f * t};
    Kit_mesh_builder builder;
    std::array<uint32_t, 24> grid{};
    for (std::size_t iz = 0; iz < 2; ++iz) {
        for (std::size_t iy = 0; iy < 3; ++iy) {
            for (std::size_t ix = 0; ix < 4; ++ix) {
                grid[(iz * 12) + (iy * 4) + ix] = builder.add_vertex({xs[ix], ys[iy], zs[iz]});
            }
        }
    }
    constexpr std::size_t front = 0;
    constexpr std::size_t back  = 1;
    const auto v = [&](const std::size_t ix, const std::size_t iy, const std::size_t iz) -> uint32_t {
        return grid[(iz * 12) + (iy * 4) + ix];
    };

    for (std::size_t iy = 0; iy < 2; ++iy) {
        for (std::size_t ix = 0; ix < 3; ++ix) {
            if ((ix == 1) && (iy == 0)) {
                continue;
            }
            builder.add_quad(v(ix, iy, front), v(ix + 1, iy, front), v(ix + 1, iy + 1, front), v(ix, iy + 1, front));
            builder.add_quad(v(ix, iy, back),  v(ix, iy + 1, back),  v(ix + 1, iy + 1, back),  v(ix + 1, iy, back));
        }
        builder.add_quad(v(0, iy, front), v(0, iy + 1, front), v(0, iy + 1, back), v(0, iy, back)); // -x end
        builder.add_quad(v(3, iy, front), v(3, iy, back), v(3, iy + 1, back), v(3, iy + 1, front)); // +x end
    }
    for (std::size_t ix = 0; ix < 3; ++ix) {
        builder.add_quad(v(ix, 2, front), v(ix + 1, 2, front), v(ix + 1, 2, back), v(ix, 2, back)); // top
    }
    for (const std::size_t ix : {std::size_t{0}, std::size_t{2}}) {
        builder.add_quad(v(ix, 0, front), v(ix, 0, back), v(ix + 1, 0, back), v(ix + 1, 0, front)); // bottom
    }
    builder.add_quad(v(1, 0, front), v(1, 0, back), v(1, 1, back), v(1, 1, front)); // left jamb, faces +x
    builder.add_quad(v(2, 0, front), v(2, 1, front), v(2, 1, back), v(2, 0, back)); // right jamb, faces -x
    builder.add_quad(v(1, 1, front), v(1, 1, back), v(2, 1, back), v(2, 1, front)); // soffit, faces -y
    builder.write(mesh);
}

void generate_pillar(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float w = values.get_float("width");
    const float h = values.get_float("height");
    Kit_mesh_builder builder;
    if (values.get_enum("profile") == "round") {
        builder.add_prism(values.get_int("segments"), 0.5f * w, 0.0f, h, 0.0f);
    } else {
        builder.add_box(glm::vec3{-0.5f * w, 0.0f, -0.5f * w}, glm::vec3{0.5f * w, h, 0.5f * w});
    }
    builder.write(mesh);
}

class Arch_points
{
public:
    std::vector<glm::vec2> inner;
    std::vector<glm::vec2> outer;
};

// Half-elliptic intrados of the span and rise, extrados offset by the thickness; mirrored so both feet are exact.
auto make_arch_points(const Kit_parameter_values& values) -> Arch_points
{
    const float a = 0.5f * values.get_float("span");
    const float b = values.get_float("rise");
    const float t = values.get_float("thickness");
    const int   n = values.get_int("segments");
    Arch_points points;
    points.inner.resize(static_cast<std::size_t>(n) + 1);
    points.outer.resize(static_cast<std::size_t>(n) + 1);
    for (int i = 0; (2 * i) <= n; ++i) {
        const float angle = std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(n);
        const bool  foot  = (i == 0);
        const bool  crown = ((2 * i) == n);
        const float c     = crown ? 0.0f : std::cos(angle);
        const float s     = foot  ? 0.0f : std::sin(angle);
        const glm::vec2 inner{a * c, b * s};
        const glm::vec2 outer{(a + t) * c, (b + t) * s};
        points.inner[static_cast<std::size_t>(i)]     = inner;
        points.outer[static_cast<std::size_t>(i)]     = outer;
        points.inner[static_cast<std::size_t>(n - i)] = glm::vec2{-inner.x, inner.y};
        points.outer[static_cast<std::size_t>(n - i)] = glm::vec2{-outer.x, outer.y};
    }
    return points;
}

void generate_arch(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float        z      = 0.5f * values.get_float("depth");
    const Arch_points  points = make_arch_points(values);
    const std::size_t  count  = points.inner.size();
    Kit_mesh_builder   builder;
    std::vector<uint32_t> inner_front(count);
    std::vector<uint32_t> inner_back (count);
    std::vector<uint32_t> outer_front(count);
    std::vector<uint32_t> outer_back (count);
    for (std::size_t i = 0; i < count; ++i) {
        inner_front[i] = builder.add_vertex({points.inner[i].x, points.inner[i].y,  z});
        inner_back [i] = builder.add_vertex({points.inner[i].x, points.inner[i].y, -z});
        outer_front[i] = builder.add_vertex({points.outer[i].x, points.outer[i].y,  z});
        outer_back [i] = builder.add_vertex({points.outer[i].x, points.outer[i].y, -z});
    }
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const std::size_t j = i + 1;
        builder.add_quad(inner_front[i], outer_front[i], outer_front[j], inner_front[j]); // front
        builder.add_quad(inner_back[i],  inner_back[j],  outer_back[j],  outer_back[i]);  // back
        builder.add_quad(inner_front[i], inner_front[j], inner_back[j],  inner_back[i]);  // intrados
        builder.add_quad(outer_front[i], outer_back[i],  outer_back[j],  outer_front[j]); // extrados
    }
    const std::size_t last = count - 1;
    builder.add_quad(inner_front[0],    inner_back[0],    outer_back[0],    outer_front[0]);    // right foot
    builder.add_quad(outer_front[last], outer_back[last], inner_back[last], inner_front[last]); // left foot
    builder.write(mesh);
}

auto check_opening(const Kit_shape_definition& shape, const float width, const float height, const Kit_agent& agent) -> std::string
{
    const float min_width  = (2.0f * agent.radius) + kit_opening_margin;
    const float min_height = agent.height + kit_opening_margin;
    if (width < min_width) {
        return fmt::format("{}: opening width {} m is narrower than the agent's {} m", shape.id, width, min_width);
    }
    if (height < min_height) {
        return fmt::format("{}: opening height {} m is lower than the agent's {} m", shape.id, height, min_height);
    }
    return {};
}

} // anonymous namespace

auto get_arch_clear_width(const Kit_parameter_values& values, const float height) -> float
{
    const Arch_points points = make_arch_points(values);
    for (std::size_t i = 0; i + 1 < points.inner.size(); ++i) {
        const glm::vec2 p0 = points.inner[i];
        const glm::vec2 p1 = points.inner[i + 1];
        if ((p0.x <= 0.0f) || (p0.y > height) || (p1.y < height)) {
            continue;
        }
        const float dy = p1.y - p0.y;
        const float t  = (dy > 0.0f) ? ((height - p0.y) / dy) : 0.0f;
        return 2.0f * std::max(0.0f, p0.x + (t * (p1.x - p0.x)));
    }
    return 0.0f;
}

void add_architecture_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.wall";
        shape.kit             = "architecture";
        shape.display_name    = "Wall";
        shape.description     = "Straight wall along X with its faces toward +Z and -Z; origin at the center of its base.";
        shape.tags            = {"obstacle", "wall", "tiles", "size:4x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",    "Size along X", 4.0f,  Kit_default_scale::modules,      0.1f,  64.0f), 1.0f,  6.0f,  0.5f),
            derive_float(make_length_parameter("height",    "Size along Y", 1.0f,  Kit_default_scale::wall_heights, 0.1f,  32.0f), 0.75f, 1.25f, 0.05f),
            derive_float(make_length_parameter("thickness", "Size along Z", 0.25f, Kit_default_scale::meters,       0.02f, 4.0f),  0.15f, 0.4f,  0.05f),
            make_seed_parameter()
        };
        shape.material_slot   = "wall";
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 12;
        shape.generator       = generate_wall;
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return all_sides(); };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.door_wall";
        shape.kit             = "architecture";
        shape.display_name    = "Wall with door opening";
        shape.description     = "Wall along X with a door opening from the floor at its center; ends and top meet plain walls.";
        shape.tags            = {"obstacle", "wall", "opening", "tiles", "size:4x1"};
        // The ranges alone keep both jambs and the header at least 0.1 m thick; derived openings stay walkable (R2.10).
        shape.parameters      = {
            derive_float(make_length_parameter("length",         "Size along X",            4.0f,  Kit_default_scale::modules,      2.0f,  64.0f), 2.5f,  6.0f,  0.5f),
            derive_float(make_length_parameter("height",         "Size along Y",            1.0f,  Kit_default_scale::wall_heights, 2.5f,  32.0f), 0.85f, 1.25f, 0.05f),
            derive_float(make_length_parameter("thickness",      "Size along Z",            0.25f, Kit_default_scale::meters,       0.02f, 4.0f),  0.15f, 0.4f,  0.05f),
            derive_float(make_length_parameter("opening_width",  "Clear width of the door", 1.2f,  Kit_default_scale::meters,       0.3f,  1.8f),  0.9f,  1.6f,  0.1f),
            derive_float(make_length_parameter("opening_height", "Clear height of the door",2.2f,  Kit_default_scale::meters,       0.5f,  2.4f),  2.0f,  2.4f,  0.05f),
            make_seed_parameter()
        };
        shape.material_slot   = "wall";
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 44;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_door_wall;
        shape.get_seam_sides  = [](const Kit_parameter_values&) {
            return std::vector<Kit_side>{Kit_side::x_neg, Kit_side::x_pos, Kit_side::y_pos};
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            return check_opening(*values.get_shape(), values.get_float("opening_width"), values.get_float("opening_height"), agent);
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.pillar";
        shape.kit             = "architecture";
        shape.display_name    = "Pillar";
        shape.description     = "Upright pillar, square or round, with its origin at the center of its base.";
        shape.tags            = {"obstacle", "pillar", "size:0.5x1"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("profile",  "Cross section",         {"square", "round"}, 0)),
            derive_float(make_length_parameter("width",    "Width or diameter",     0.5f, Kit_default_scale::modules,      0.05f, 16.0f), 0.3f, 1.0f, 0.05f),
            derive_float(make_length_parameter("height",   "Size along Y",          1.0f, Kit_default_scale::wall_heights, 0.1f,  64.0f), 0.5f, 1.5f, 0.05f),
            make_count_parameter ("segments", "Sides of a round pillar", 12, 3, 64),
            make_seed_parameter  ()
        };
        shape.material_slot           = "trim";
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget         = 12;
        shape.generator               = generate_pillar;
        shape.get_seam_sides          = [](const Kit_parameter_values& values) {
            return (values.get_enum("profile") == "square") ? all_sides() : std::vector<Kit_side>{};
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.arch";
        shape.kit             = "architecture";
        shape.display_name    = "Arch";
        shape.description     =
            "Free-standing arch ring across X with a half-elliptic opening of the span and rise, standing on its two feet; "
            "its faces point toward +Z and -Z.";
        shape.tags            = {"obstacle", "opening", "arch", "size:2x1"};
        shape.parameters      = {
            // The narrowest derived opening (span 2 modules, rise 0.8 wall heights) is the default's, which is walkable (R2.10).
            derive_float(make_length_parameter("span",      "Clear width of the opening at the base", 2.0f, Kit_default_scale::modules,      0.3f,  32.0f), 2.0f, 4.0f, 0.25f),
            derive_float(make_length_parameter("rise",      "Clear height of the opening",            0.8f, Kit_default_scale::wall_heights, 0.2f,  32.0f), 0.8f, 1.2f, 0.05f),
            derive_float(make_length_parameter("depth",     "Size along Z",                           0.5f, Kit_default_scale::modules,      0.05f, 16.0f), 0.3f, 1.0f, 0.05f),
            derive_float(make_length_parameter("thickness", "Thickness of the ring",                  0.3f, Kit_default_scale::meters,       0.05f, 8.0f),  0.2f, 0.5f, 0.05f),
            make_count_parameter ("segments",  "Segments along the curve",               12, 2, 48),
            make_seed_parameter  ()
        };
        shape.material_slot           = "trim";
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget         = 100;
        shape.collision               = Kit_collision::triangle_mesh;
        shape.generator               = generate_arch;
        shape.check_walkability       = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float clear_height = agent.height + kit_opening_margin;
            return check_opening(*values.get_shape(), get_arch_clear_width(values, clear_height), values.get_float("rise"), agent);
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
