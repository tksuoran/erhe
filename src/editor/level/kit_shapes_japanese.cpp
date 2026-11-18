#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace editor {

namespace {

constexpr int torii_post_sides = 12;

// The torii's members (doc/editor/level_kit_shapes.md "Torii"), from its values.
class Torii_layout
{
public:
    explicit Torii_layout(const Kit_parameter_values& values)
        : width     {values.get_float("width")}
        , height    {values.get_float("height")}
        , post      {values.get_float("post_width")}
        , lean      {std::tan(glm::radians(values.get_float("lean")))}
        , curve_rise{values.get_float("curve") * values.get_float("width")}
    {
        kasagi_height  = 0.55f * post;
        kasagi_depth   = 1.25f * post;
        kasagi_half    = (0.75f * width) + post;
        shimaki_height = 0.45f * post;
        shimaki_depth  = 1.05f * post;
        shimaki_half   = kasagi_half - (0.3f * post);
        nuki_y         = 0.74f * height;
        nuki_height    = 0.5f * post;
        nuki_depth     = 0.45f * post;
        nuki_half      = (0.5f * width) + (1.2f * post);
    }

    // The underside of the top lintel at x: it turns up toward the ends.
    [[nodiscard]] auto kasagi_bottom(const float x) const -> float
    {
        const float u = x / kasagi_half;
        return (height - kasagi_height) + (curve_rise * u * u);
    }

    [[nodiscard]] auto post_top_y(const float x) const -> float
    {
        return kasagi_bottom(x) - (0.5f * shimaki_height);
    }

    // Clear width between the posts at the height y.
    [[nodiscard]] auto clear_width(const float y) const -> float
    {
        return width - post - (2.0f * y * lean);
    }

    float width;
    float height;
    float post;
    float lean; // tangent of the posts' inward lean
    float curve_rise;
    float kasagi_height {0.0f};
    float kasagi_depth  {0.0f};
    float kasagi_half   {0.0f};
    float shimaki_height{0.0f};
    float shimaki_depth {0.0f};
    float shimaki_half  {0.0f};
    float nuki_y        {0.0f};
    float nuki_height   {0.0f};
    float nuki_depth    {0.0f};
    float nuki_half     {0.0f};
};

// A prism of horizontal caps from the bottom center to the top center: a leaning post whose foot lies flat.
void add_sheared_prism(Kit_mesh_builder& builder, const int sides, const float radius, const glm::vec3& bottom, const glm::vec3& top)
{
    std::vector<uint32_t> lower;
    std::vector<uint32_t> upper;
    for (int i = 0; i < sides; ++i) {
        const float     angle  = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(sides);
        const glm::vec3 offset{radius * std::cos(angle), 0.0f, -radius * std::sin(angle)};
        lower.push_back(builder.add_vertex(bottom + offset));
        upper.push_back(builder.add_vertex(top + offset));
    }
    const std::vector<uint32_t> bottom_cap{lower.rbegin(), lower.rend()};
    builder.add_polygon(upper);
    builder.add_polygon(bottom_cap);
    for (std::size_t i = 0; i < lower.size(); ++i) {
        const std::size_t j = (i + 1) % lower.size();
        builder.add_quad(lower[i], lower[j], upper[j], upper[i]);
    }
}

// A beam along X of the given half length and depth whose underside follows bottom(x) at the stations: a closed strip.
template <typename Bottom>
void add_curved_beam(Kit_mesh_builder& builder, const int stations, const float half_length, const float depth, const float beam_height, const Bottom& bottom)
{
    const float half_depth = 0.5f * depth;
    // Per station: bottom front, top front, top back, bottom back.
    std::vector<std::array<uint32_t, 4>> rings;
    for (int i = 0; i <= stations; ++i) {
        const float x = (static_cast<float>((2 * i) - stations) / static_cast<float>(stations)) * half_length;
        const float y = bottom(x);
        rings.push_back({
            builder.add_vertex(glm::vec3{x, y, half_depth}),
            builder.add_vertex(glm::vec3{x, y + beam_height, half_depth}),
            builder.add_vertex(glm::vec3{x, y + beam_height, -half_depth}),
            builder.add_vertex(glm::vec3{x, y, -half_depth})
        });
    }
    for (std::size_t i = 0; (i + 1) < rings.size(); ++i) {
        const std::array<uint32_t, 4>& a = rings[i];
        const std::array<uint32_t, 4>& b = rings[i + 1];
        builder.add_quad(a[0], b[0], b[1], a[1]); // front
        builder.add_quad(a[1], b[1], b[2], a[2]); // top
        builder.add_quad(a[2], b[2], b[3], a[3]); // back
        builder.add_quad(a[3], b[3], b[0], a[0]); // underside
    }
    const std::array<uint32_t, 4>& first = rings.front();
    const std::array<uint32_t, 4>& last  = rings.back();
    builder.add_quad(first[0], first[1], first[2], first[3]);
    builder.add_quad(last[3], last[2], last[1], last[0]);
}

void generate_torii(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Torii_layout torii{values};
    const int          stations = 2 * std::max(1, values.get_int("segments") / 2); // even, so the stations mirror across x = 0
    Kit_mesh_builder   builder;

    // Posts on stone sleeves, leaning in so their tops are closer than their feet.
    const float sleeve_height = std::min(0.12f * torii.height, 1.2f * torii.post);
    for (const float side : {-1.0f, 1.0f}) {
        const float     foot_x = side * 0.5f * torii.width;
        const float     top_y  = torii.post_top_y(foot_x - (side * torii.lean * torii.height));
        const glm::vec3 bottom{foot_x, 0.0f, 0.0f};
        const glm::vec3 top   {foot_x - (side * top_y * torii.lean), top_y, 0.0f};
        add_sheared_prism(builder, torii_post_sides, 0.5f * torii.post, bottom, top);
        add_sheared_prism(builder, torii_post_sides, 0.62f * torii.post, bottom, bottom + glm::vec3{0.0f, sleeve_height, 0.0f});
    }

    // The straight tie beam through the posts, and the strut from it to the lower lintel.
    builder.add_beam(glm::vec3{-torii.nuki_half, torii.nuki_y, 0.0f}, glm::vec3{torii.nuki_half, torii.nuki_y, 0.0f}, torii.nuki_depth, torii.nuki_height);
    const float strut_bottom = torii.nuki_y + (0.5f * torii.nuki_height) - (0.02f * torii.post);
    const float strut_top    = torii.kasagi_bottom(0.0f) - (0.5f * torii.shimaki_height);
    if (strut_top > (strut_bottom + (0.2f * torii.post))) {
        builder.add_beam(glm::vec3{0.0f, strut_bottom, 0.0f}, glm::vec3{0.0f, strut_top, 0.0f}, 0.4f * torii.post, 0.5f * torii.post);
    }

    // The lower lintel under the top lintel, both following its curve.
    add_curved_beam(builder, stations, torii.shimaki_half, torii.shimaki_depth, torii.shimaki_height + (0.01f * torii.post),
        [&torii](const float x) { return torii.kasagi_bottom(x) - torii.shimaki_height; });
    add_curved_beam(builder, stations, torii.kasagi_half, torii.kasagi_depth, torii.kasagi_height,
        [&torii](const float x) { return torii.kasagi_bottom(x); });
    builder.write(mesh);
}

} // anonymous namespace

void add_japanese_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.torii";
        shape.kit             = "architecture";
        shape.display_name    = "Torii";
        shape.description     =
            "Japanese gate across X standing at its origin: two posts on stone sleeves leaning in, a straight tie beam "
            "through them with a strut, and a lower and a top lintel whose ends turn up by the curve; walk through it "
            "along Z.";
        shape.tags            = {"obstacle", "opening", "gate", "japanese", "size:3x1.2"};
        shape.parameters      = {
            // In meters: the agent fixes the least opening, whatever the module and wall height.
            derive_float(make_length_parameter("width",      "Distance of the posts' feet", 3.0f, Kit_default_scale::meters, 1.0f, 12.0f), 2.5f, 5.0f, 0.25f),
            derive_float(make_length_parameter("height",     "Height of the top lintel's top at the center", 3.6f, Kit_default_scale::meters, 2.0f, 15.0f), 3.2f, 4.8f, 0.1f),
            from_seed_by_default(derive_float(make_ratio_parameter("curve", "How far the top lintel's ends turn up, a share of the width", 0.08f), 0.0f, 0.15f, 0.01f)),
            derive_float(make_length_parameter("post_width", "Diameter of a post", 0.3f, Kit_default_scale::meters, 0.1f, 1.0f), 0.2f, 0.45f, 0.01f),
            derive_float(make_angle_parameter ("lean",       "Inward lean of the posts", 2.0f, 0.0f, 8.0f), 0.0f, 4.0f, 0.5f),
            make_seed_parameter  (),
            make_count_parameter ("segments", "Stations along the lintels", 12, 2, 24)
        };
        shape.parameters[2].float_max = 0.2f;
        shape.material_slots  = {"wood"};
        shape.normal_style    = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 40.0f;
        shape.triangle_budget = 420;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_torii;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.check_combination = [](const Kit_parameter_values& values) -> std::string {
            const Torii_layout torii{values};
            if (torii.clear_width(torii.post_top_y(0.0f)) < torii.post) {
                return fmt::format("{}: posts leaning {} degrees over a height of {} m meet; lean less or widen the gate", values.get_shape()->id, values.get_float("lean"), torii.height);
            }
            return {};
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Torii_layout torii{values};
            const float clear_height = torii.nuki_y - (0.5f * torii.nuki_height);
            return check_kit_opening(*values.get_shape(), torii.clear_width(agent.height + kit_opening_margin), clear_height, agent);
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_depth = 0.5f * values.get_float("post_width");
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
            add_side_connector(out_connectors, "opening_front", glm::vec3{0.0f, 0.0f,  half_depth}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "opening_back",  glm::vec3{0.0f, 0.0f, -half_depth}, glm::vec3{0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
