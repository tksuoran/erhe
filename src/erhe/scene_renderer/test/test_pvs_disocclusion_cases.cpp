#include "test_pvs_disocclusion_cases.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <random>
#include <utility>

namespace erhe::scene_renderer::test {

namespace {

[[nodiscard]] auto region_words() -> uint32_t
{
    return c_pvs_test_resolution * c_pvs_test_resolution * c_pvs_test_layer_count;
}

[[nodiscard]] auto rectangle_contains(const glm::uvec4& rectangle, const uint32_t x, const uint32_t y) -> bool
{
    return (x >= rectangle.x) && (x < rectangle.z) && (y >= rectangle.y) && (y < rectangle.w);
}

} // anonymous namespace

namespace {

// Every column far behind the face, unused.
void reset_column_bounds(Pvs_test_case& test_case, const uint32_t column_count)
{
    test_case.column_bounds.assign(2u * static_cast<std::size_t>(column_count), glm::vec4{0.0f});
    test_case.column_used.assign(column_count, uint8_t{0});
    for (uint32_t column = 0; column < column_count; ++column) {
        test_case.column_bounds[(2u * column) + 0u] = glm::vec4{-0.1f, -0.1f, -100.0f, 0.0f};
        test_case.column_bounds[(2u * column) + 1u] = glm::vec4{ 0.1f,  0.1f,  -99.0f, 0.0f};
    }
}

// A point of the pixel's central ray at the middle of the layer's depth
// range, as a tiny box.
void grow_by_sample(Pvs_test_case& test_case, const uint32_t pixel_x, const uint32_t pixel_y, const uint32_t layer, const uint32_t id)
{
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    const float z  = 0.5f * (parameters.layers[layer].x + parameters.layers[layer + 1u].x);
    const float tx = parameters.tangents.x + ((static_cast<float>(pixel_x) + 0.5f) * parameters.tangents.y);
    const float ty = parameters.tangents.z + ((static_cast<float>(pixel_y) + 0.5f) * parameters.tangents.w);
    const glm::vec3 point{tx * z, ty * z, z};
    const glm::vec3 epsilon{1.0e-4f * z};
    add_pvs_test_bounds(test_case, id, point - epsilon, point + epsilon);
}

} // anonymous namespace

auto make_pvs_test_parameters() -> Pvs_disocclusion_parameters
{
    const float pixel_tangent = 2.0f / static_cast<float>(c_pvs_test_resolution);
    return make_pvs_disocclusion_parameters(
        Pvs_face_geometry{
            .resolution  = c_pvs_test_resolution,
            .tile_size   = c_pvs_test_tile_size,
            .layer_count = c_pvs_test_layer_count,
            .z_near      = 1.0f,
            .z_far       = 16.0f,
            .tan_x0      = -1.0f,
            .tan_dx      = pixel_tangent,
            .tan_y0      = 1.0f,
            .tan_dy      = -pixel_tangent,
            .cell_min    = glm::vec3{-0.5f, -0.5f, -0.5f},
            .cell_max    = glm::vec3{ 0.5f,  0.5f,  0.5f}
        },
        c_pvs_test_columns
    );
}

auto make_pvs_test_case(const std::string& name) -> Pvs_test_case
{
    Pvs_test_case test_case{
        .name          = name,
        .parameters    = make_pvs_test_parameters(),
        .layered_words = {}
    };
    test_case.layered_words.resize(3u * static_cast<std::size_t>(region_words()), 0u);
    // depth and id_min cleared to 0xffffffff, id_max to 0.
    std::fill(test_case.layered_words.begin(), test_case.layered_words.begin() + (2u * static_cast<std::size_t>(region_words())), 0xffffffffu);
    reset_column_bounds(test_case, c_pvs_test_columns);
    return test_case;
}

void add_pvs_test_bounds(Pvs_test_case& test_case, const uint32_t column, const glm::vec3& box_min, const glm::vec3& box_max)
{
    ERHE_VERIFY((2u * static_cast<std::size_t>(column)) < test_case.column_bounds.size());
    glm::vec4& bounds_min = test_case.column_bounds[(2u * column) + 0u];
    glm::vec4& bounds_max = test_case.column_bounds[(2u * column) + 1u];
    if (test_case.column_used[column] == 0u) {
        bounds_min = glm::vec4{box_min, 0.0f};
        bounds_max = glm::vec4{box_max, 0.0f};
        test_case.column_used[column] = 1u;
    } else {
        bounds_min = glm::vec4{glm::min(glm::vec3{bounds_min}, box_min), 0.0f};
        bounds_max = glm::vec4{glm::max(glm::vec3{bounds_max}, box_max), 0.0f};
    }
}

void set_pvs_test_sample(Pvs_test_case& test_case, const uint32_t pixel_x, const uint32_t pixel_y, const uint32_t layer, const uint32_t id)
{
    ERHE_VERIFY((pixel_x < c_pvs_test_resolution) && (pixel_y < c_pvs_test_resolution) && (layer < c_pvs_test_layer_count));
    const uint32_t word = (layer * c_pvs_test_resolution * c_pvs_test_resolution) + (pixel_y * c_pvs_test_resolution) + pixel_x;
    test_case.layered_words[word]                         = std::bit_cast<uint32_t>(test_case.parameters.layers[layer].x);
    test_case.layered_words[region_words() + word]        = id;
    test_case.layered_words[(2u * region_words()) + word] = id;
    if (id < (test_case.column_bounds.size() / 2u)) {
        grow_by_sample(test_case, pixel_x, pixel_y, layer, id);
    }
}

void set_pvs_test_depth(Pvs_test_case& test_case, const uint32_t pixel_x, const uint32_t pixel_y, const float depth, const uint32_t id)
{
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    ERHE_VERIFY((pixel_x < c_pvs_test_resolution) && (pixel_y < c_pvs_test_resolution));
    ERHE_VERIFY((depth >= parameters.layers[0].x) && (depth < parameters.layers[c_pvs_test_layer_count].x));
    uint32_t layer = 0;
    while (((layer + 1u) < c_pvs_test_layer_count) && (depth >= parameters.layers[layer + 1u].x)) {
        ++layer;
    }
    const uint32_t word = (layer * c_pvs_test_resolution * c_pvs_test_resolution) + (pixel_y * c_pvs_test_resolution) + pixel_x;
    test_case.layered_words[word]                         = std::bit_cast<uint32_t>(depth);
    test_case.layered_words[region_words() + word]        = id;
    test_case.layered_words[(2u * region_words()) + word] = id;
    if (id < (test_case.column_bounds.size() / 2u)) {
        const float tx = parameters.tangents.x + ((static_cast<float>(pixel_x) + 0.5f) * parameters.tangents.y);
        const float ty = parameters.tangents.z + ((static_cast<float>(pixel_y) + 0.5f) * parameters.tangents.w);
        const glm::vec3 point{tx * depth, ty * depth, depth};
        const glm::vec3 epsilon{1.0e-4f * depth};
        add_pvs_test_bounds(test_case, id, point - epsilon, point + epsilon);
    }
}

namespace {

// Entry c_rule_3_behind: a box behind every surface of the rule 3 cases,
// in tile columns (3, 3) .. (4, 3) at the depths of layers 6 .. 7.
void add_rule_3_behind(Pvs_test_case& test_case)
{
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    const float k  = static_cast<float>(c_pvs_test_tile_size);
    const float z0 = parameters.layers[6].x * 1.01f;
    const float z1 = parameters.layers[7].x * 1.5f;
    const float ax = parameters.tangents.x + ((3.25f * k) * parameters.tangents.y);
    const float bx = parameters.tangents.x + ((4.75f * k) * parameters.tangents.y);
    const float ay = parameters.tangents.z + ((3.25f * k) * parameters.tangents.w);
    const float by = parameters.tangents.z + ((3.75f * k) * parameters.tangents.w);
    const float tx_min = std::min(ax, bx);
    const float tx_max = std::max(ax, bx);
    const float ty_min = std::min(ay, by);
    const float ty_max = std::max(ay, by);
    add_pvs_test_bounds(
        test_case, c_rule_3_behind,
        glm::vec3{std::max(tx_min * z0, tx_min * z1), std::max(ty_min * z0, ty_min * z1), z0},
        glm::vec3{std::min(tx_max * z0, tx_max * z1), std::min(ty_max * z0, ty_max * z1), z1}
    );
}

} // anonymous namespace

auto make_rule_3_stair_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_3_stair");
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    // From layer 1's front at x = 0 to layer 5's front at x = 15, linearly.
    const float z0 = parameters.layers[1].x;
    const float z1 = parameters.layers[5].x;
    for (uint32_t py = 0; py < c_pvs_test_resolution; ++py) {
        for (uint32_t px = 0; px < c_pvs_test_resolution; ++px) {
            const float t = static_cast<float>(px) / static_cast<float>(c_pvs_test_resolution - 1u);
            set_pvs_test_depth(test_case, px, py, z0 + (t * (z1 - z0)), 1);
        }
    }
    add_rule_3_behind(test_case);
    test_case.parameters.columns.z = 1u; // rule 3 on
    return test_case;
}

auto make_rule_3_small_step_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_3_small_step");
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    const float z0    = parameters.layers[1].x;
    const float z1    = parameters.layers[5].x;
    const float slope = (z1 - z0) / static_cast<float>(c_pvs_test_resolution - 1u);
    for (uint32_t py = 0; py < c_pvs_test_resolution; ++py) {
        for (uint32_t px = 0; px < c_pvs_test_resolution; ++px) {
            // Pixel columns 10.. are 0.5 slope deeper: the step from 9 to
            // 10 (tile columns 4 and 5) is 1.5 slopes.
            const float extra = (px >= 10u) ? (0.5f * slope) : 0.0f;
            set_pvs_test_depth(test_case, px, py, z0 + (static_cast<float>(px) * slope) + extra, 1);
        }
    }
    add_rule_3_behind(test_case);
    test_case.parameters.columns.z = 1u; // rule 3 on
    return test_case;
}

auto make_rule_3_ledge_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_3_ledge");
    const float pixel_tangent = 2.0f / static_cast<float>(c_pvs_test_resolution);
    test_case.parameters = make_pvs_disocclusion_parameters(
        Pvs_face_geometry{
            .resolution  = c_pvs_test_resolution,
            .tile_size   = 4,
            .layer_count = c_pvs_test_layer_count,
            .z_near      = 1.0f,
            .z_far       = 16.0f,
            .tan_x0      = -1.0f,
            .tan_dx      = pixel_tangent,
            .tan_y0      = 1.0f,
            .tan_dy      = -pixel_tangent,
            .cell_min    = glm::vec3{-0.5f, -0.5f, -0.5f},
            .cell_max    = glm::vec3{ 0.5f,  0.5f,  0.5f}
        },
        c_pvs_test_columns
    );
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    const float d1       = parameters.layers[2].x;
    const float boundary = parameters.layers[3].x;
    const float slope    = 0.02f * boundary;
    const float d2       = boundary - (0.5f * slope); // pixel column 8
    for (uint32_t py = 4; py < 8; ++py) {
        for (uint32_t px = 4; px < 12; ++px) {
            const float depth = (py < 6u)
                ? d1
                : (d2 + ((static_cast<float>(px) - 8.0f) * slope));
            set_pvs_test_depth(test_case, px, py, depth, 1);
        }
    }
    test_case.parameters.columns.z = 1u; // rule 3 on
    return test_case;
}

auto make_rule_1_step_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_1_step");
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    for (uint32_t py = 6; py < 8; ++py) {
        set_pvs_test_depth(test_case, 8, py, parameters.layers[2].x, 1);
        set_pvs_test_depth(test_case, 9, py, parameters.layers[4].x * 0.999f, 2);
    }
    return test_case;
}

auto make_epsilon_case(const Pvs_epsilon_step step) -> Pvs_test_case
{
    const bool step_cm = (step == Pvs_epsilon_step::one_centimeter);
    Pvs_test_case test_case = make_pvs_test_case(step_cm ? "epsilon_step" : "epsilon_ulp");
    const float depth = 10.0f;
    for (uint32_t py = 0; py < c_pvs_test_resolution; ++py) {
        for (uint32_t px = 0; px < c_pvs_test_resolution; ++px) {
            float d = ((py & 1u) != 0u) ? std::nextafter(depth, 20.0f) : depth;
            if (step_cm && (px >= 9u)) {
                d += 0.01f;
            }
            set_pvs_test_depth(test_case, px, py, d, 1);
        }
    }
    return test_case;
}

auto make_word_boundary_case() -> Pvs_test_case
{
    // 16 x 16 pixels, K = 2, the production layer count 64.
    constexpr uint32_t resolution  = 16;
    constexpr uint32_t tile_size   = 2;
    constexpr uint32_t layer_count = 64;
    const float pixel_tangent = 2.0f / static_cast<float>(resolution);
    Pvs_test_case test_case{
        .name       = "word_boundary",
        .parameters = make_pvs_disocclusion_parameters(
            Pvs_face_geometry{
                .resolution  = resolution,
                .tile_size   = tile_size,
                .layer_count = layer_count,
                .z_near      = 1.0f,
                .z_far       = 64.0f,
                .tan_x0      = -1.0f,
                .tan_dx      = pixel_tangent,
                .tan_y0      = 1.0f,
                .tan_dy      = -pixel_tangent,
                .cell_min    = glm::vec3{-0.5f, -0.5f, -0.5f},
                .cell_max    = glm::vec3{ 0.5f,  0.5f,  0.5f}
            },
            c_pvs_test_columns
        ),
        .layered_words = {}
    };
    reset_column_bounds(test_case, c_pvs_test_columns);
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    const std::size_t region = static_cast<std::size_t>(resolution) * resolution * layer_count;
    test_case.layered_words.resize(3u * region, 0u);
    std::fill(test_case.layered_words.begin(), test_case.layered_words.begin() + (2u * region), 0xffffffffu);
    const auto set = [&](const uint32_t px, const uint32_t py, const float depth, const uint32_t id) -> void {
        uint32_t layer = 0;
        while (((layer + 1u) < layer_count) && (depth >= parameters.layers[layer + 1u].x)) {
            ++layer;
        }
        const std::size_t w = (static_cast<std::size_t>(layer) * resolution * resolution) + (static_cast<std::size_t>(py) * resolution) + px;
        test_case.layered_words[w]                 = std::bit_cast<uint32_t>(depth);
        test_case.layered_words[region + w]        = id;
        test_case.layered_words[(2u * region) + w] = id;
        const float tx = parameters.tangents.x + ((static_cast<float>(px) + 0.5f) * parameters.tangents.y);
        const float ty = parameters.tangents.z + ((static_cast<float>(py) + 0.5f) * parameters.tangents.w);
        add_pvs_test_bounds(test_case, id, glm::vec3{tx * depth, ty * depth, depth} - glm::vec3{1.0e-4f * depth}, glm::vec3{tx * depth, ty * depth, depth} + glm::vec3{1.0e-4f * depth});
    };
    const float l31 = parameters.layers[31].x;
    const float l32 = parameters.layers[32].x;
    const float l33 = parameters.layers[33].x;
    // Pixel rows 2 .. 3, columns 0 .. 5: a plane from layer 31's front to
    // layer 33's front (continuous: runs [31, 32] and [32, 33]).
    for (uint32_t py = 2; py < 4; ++py) {
        for (uint32_t px = 0; px < 6; ++px) {
            set(px, py, l31 + ((static_cast<float>(px) / 5.0f) * (l33 - l31)), 3);
        }
    }
    // A sheet at layer 31 elsewhere with a hole at tile (5, 5), which shows a
    // sheet at layer 32 there; entries behind everything at layer 34.
    for (uint32_t py = 0; py < resolution; ++py) {
        for (uint32_t px = 0; px < resolution; ++px) {
            const uint32_t tx = px / tile_size;
            const uint32_t ty = py / tile_size;
            if ((ty == 1u) && (tx <= 2u)) {
                continue;
            }
            if (!((tx == 5u) && (ty == 5u))) {
                set(px, py, l31, 1);
            } else {
                set(px, py, l32, 2);
            }
            set(px, py, parameters.layers[34].x, 4 + (tx % 8u));
        }
    }
    return test_case;
}

auto make_rule_3_box_edge_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_3_box_edge");
    for (uint32_t y = 0; y < c_pvs_test_tiles; ++y) {
        for (uint32_t x = 0; x < c_pvs_test_tiles; ++x) {
            fill_pvs_test_tile(test_case, x, y, (x <= 3u) ? 2u : 7u, (x <= 3u) ? 1u : 2u);
        }
    }
    add_rule_3_behind(test_case);
    test_case.parameters.columns.z = 1u; // rule 3 on
    return test_case;
}

auto make_rule_3_crease_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_3_crease");
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    // Nearest at the boundary between pixel columns 7 and 8 (layer 2's
    // front), growing by a quarter layer-2 depth per pixel to both sides.
    const float z_edge = parameters.layers[2].x;
    const float slope  = 0.06f * z_edge;
    for (uint32_t py = 0; py < c_pvs_test_resolution; ++py) {
        for (uint32_t px = 0; px < c_pvs_test_resolution; ++px) {
            const float distance = std::abs((static_cast<float>(px) + 0.5f) - 8.0f);
            set_pvs_test_depth(test_case, px, py, z_edge + ((distance - 0.5f) * slope), 1);
        }
    }
    add_rule_3_behind(test_case);
    test_case.parameters.columns.z = 1u; // rule 3 on
    return test_case;
}

auto make_rule_3_one_row_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_3_one_row");
    for (uint32_t y = 0; y < c_pvs_test_tiles; ++y) {
        for (uint32_t x = 0; x < c_pvs_test_tiles; ++x) {
            if (x <= 3u) {
                fill_pvs_test_tile(test_case, x, y, 2, 1);
            } else {
                for (uint32_t py = 0; py < c_pvs_test_tile_size; ++py) {
                    for (uint32_t px = 0; px < c_pvs_test_tile_size; ++px) {
                        const bool even_row = (py == 0u);
                        set_pvs_test_sample(test_case, (x * c_pvs_test_tile_size) + px, (y * c_pvs_test_tile_size) + py, even_row ? 2u : 7u, even_row ? 1u : 2u);
                    }
                }
            }
        }
    }
    add_rule_3_behind(test_case);
    test_case.parameters.columns.z = 1u; // rule 3 on
    return test_case;
}

auto make_rule_3_inner_edge_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("rule_3_inner_edge");
    for (uint32_t py = 0; py < c_pvs_test_resolution; ++py) {
        for (uint32_t px = 0; px < c_pvs_test_resolution; ++px) {
            const bool sheet = (px <= 8u);
            set_pvs_test_sample(test_case, px, py, sheet ? 2u : 7u, sheet ? 1u : 2u);
        }
    }
    add_rule_3_behind(test_case);
    test_case.parameters.columns.z = 1u; // rule 3 on
    return test_case;
}

void fill_pvs_test_tile(Pvs_test_case& test_case, const uint32_t tile_x, const uint32_t tile_y, const uint32_t layer, const uint32_t id)
{
    for (uint32_t py = 0; py < c_pvs_test_tile_size; ++py) {
        for (uint32_t px = 0; px < c_pvs_test_tile_size; ++px) {
            set_pvs_test_sample(test_case, (tile_x * c_pvs_test_tile_size) + px, (tile_y * c_pvs_test_tile_size) + py, layer, id);
        }
    }
}

void fill_pvs_test_layer_except(Pvs_test_case& test_case, const uint32_t layer, const uint32_t id, const uint32_t x0, const uint32_t y0, const uint32_t x1, const uint32_t y1)
{
    for (uint32_t y = 0; y < c_pvs_test_tiles; ++y) {
        for (uint32_t x = 0; x < c_pvs_test_tiles; ++x) {
            if ((x >= x0) && (x < x1) && (y >= y0) && (y < y1)) {
                continue;
            }
            fill_pvs_test_tile(test_case, x, y, layer, id);
        }
    }
}

auto make_figure_2_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("figure_2");
    // W1 at (2, 4) of layer 1; W2 of layer 2 inside W1's cone on layer 2
    // (the rays through W1 reach it: it is a window), as far from W1 as
    // the cone allows.
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    const glm::uvec4 cone_1_on_2 = pvs_cone_rectangle(parameters, 2, 4, 1, 15u, 2);
    const uint32_t   w2_x        = cone_1_on_2.z - 1u;
    ERHE_VERIFY((w2_x > 2u) && rectangle_contains(cone_1_on_2, w2_x, 4));
    fill_pvs_test_layer_except(test_case, 1, 1, 2, 4, 3, 5);
    fill_pvs_test_layer_except(test_case, 2, 2, w2_x, 4, w2_x + 1u, 5);
    // B: a tile in both cones at layer 3; A: one outside W1's cone (behind
    // layer 1, reached through no window of it).
    const glm::uvec4 cone_1 = pvs_cone_rectangle(parameters, 2,    4, 1, 15u, 3);
    const glm::uvec4 cone_2 = pvs_cone_rectangle(parameters, w2_x, 4, 2, 15u, 3);
    glm::uvec2 b{c_pvs_test_tiles, 0u};
    glm::uvec2 a{c_pvs_test_tiles, 0u};
    for (uint32_t y = 0; y < c_pvs_test_tiles; ++y) {
        for (uint32_t x = 0; x < c_pvs_test_tiles; ++x) {
            const bool in_1 = rectangle_contains(cone_1, x, y);
            const bool in_2 = rectangle_contains(cone_2, x, y);
            if (in_1 && in_2 && (b.x == c_pvs_test_tiles)) {
                b = glm::uvec2{x, y};
            }
            static_cast<void>(in_2);
            if (!in_1 && (a.x == c_pvs_test_tiles)) {
                a = glm::uvec2{x, y};
            }
        }
    }
    ERHE_VERIFY((b.x < c_pvs_test_tiles) && (a.x < c_pvs_test_tiles));
    fill_pvs_test_tile(test_case, b.x, b.y, 3, c_figure_2_b);
    fill_pvs_test_tile(test_case, a.x, a.y, 3, c_figure_2_a);
    return test_case;
}

auto make_empty_case() -> Pvs_test_case
{
    return make_pvs_test_case("empty");
}

auto make_closed_layer_0_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("closed_layer_0");
    fill_pvs_test_layer_except(test_case, 0, 1, 0, 0, 0, 0);
    fill_pvs_test_tile(test_case, 3, 3, 3, 7);
    return test_case;
}

auto make_intersection_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("intersection");
    fill_pvs_test_layer_except(test_case, 1, 1, 3, 3, 4, 4);
    fill_pvs_test_layer_except(test_case, 2, 2, 3, 3, 4, 4);
    for (uint32_t py = 0; py < c_pvs_test_tile_size; ++py) {
        set_pvs_test_sample(test_case, (3 * c_pvs_test_tile_size) + 0, (3 * c_pvs_test_tile_size) + py, 1, 3);
        set_pvs_test_sample(test_case, (3 * c_pvs_test_tile_size) + 1, (3 * c_pvs_test_tile_size) + py, 2, 4);
    }
    fill_pvs_test_tile(test_case, 3, 3, 4, 8);
    return test_case;
}

auto make_degenerate_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("degenerate");
    fill_pvs_test_layer_except(test_case, 1, 1, 3, 3, 6, 6);
    fill_pvs_test_tile(test_case, 4, 4, 3, 6);
    return test_case;
}

auto make_grazing_band_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("grazing_band");
    // A grazing, corrugated surface from pixel column 6 on: depth 2.7 +
    // 0.6 per pixel column + 0.7 on odd pixel rows (a continuous sheet:
    // every step is the local slope). Tile column 3 (pixel columns 6, 7)
    // takes first hits in layers 2, 3 and 4; the columns x <= 2 are open
    // space.
    for (uint32_t py = 0; py < c_pvs_test_resolution; ++py) {
        for (uint32_t px = 6; px < c_pvs_test_resolution; ++px) {
            const float depth = 2.7f + (0.6f * static_cast<float>(px - 6u)) + (((py & 1u) != 0u) ? 0.7f : 0.0f);
            set_pvs_test_depth(test_case, px, py, depth, 1);
        }
    }
    fill_pvs_test_tile(test_case, 4, 3, 7, 9);
    return test_case;
}

auto make_gating_closed_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("gating_closed");
    fill_pvs_test_layer_except(test_case, 2, 2, 0, 0, 0, 0);
    fill_pvs_test_layer_except(test_case, 3, 3, 4, 4, 5, 5);
    fill_pvs_test_tile(test_case, 4, 4, 5, 5);
    return test_case;
}

auto make_gating_open_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("gating_open");
    fill_pvs_test_layer_except(test_case, 2, 2, 4, 4, 5, 5);
    fill_pvs_test_layer_except(test_case, 3, 3, 4, 4, 5, 5);
    fill_pvs_test_tile(test_case, 4, 4, 5, 5);
    return test_case;
}

auto make_bounds_vote_case() -> Pvs_test_case
{
    Pvs_test_case test_case = make_pvs_test_case("bounds_vote");
    fill_pvs_test_layer_except(test_case, 0, 1, 5, 5, 6, 6);
    // Reopen (0, 3) as well.
    for (uint32_t py = 0; py < c_pvs_test_tile_size; ++py) {
        for (uint32_t px = 0; px < c_pvs_test_tile_size; ++px) {
            const uint32_t word = (((3u * c_pvs_test_tile_size) + py) * c_pvs_test_resolution) + px;
            test_case.layered_words[word]                         = 0xffffffffu;
            test_case.layered_words[region_words() + word]        = 0xffffffffu;
            test_case.layered_words[(2u * region_words()) + word] = 0u;
        }
    }
    const Pvs_disocclusion_parameters& parameters = test_case.parameters;
    // A box whose projection stays inside tiles [x0, x1] x [y0, y1] (inset
    // by a quarter tile) at the depths z0 .. z1.
    const auto tile_box = [&](const uint32_t x0, const uint32_t y0, const uint32_t x1, const uint32_t y1, const float z0, const float z1) -> std::pair<glm::vec3, glm::vec3> {
        const float k  = static_cast<float>(c_pvs_test_tile_size);
        const float ax = parameters.tangents.x + (((static_cast<float>(x0) + 0.25f) * k) * parameters.tangents.y);
        const float bx = parameters.tangents.x + (((static_cast<float>(x1) + 0.75f) * k) * parameters.tangents.y);
        const float ay = parameters.tangents.z + (((static_cast<float>(y0) + 0.25f) * k) * parameters.tangents.w);
        const float by = parameters.tangents.z + (((static_cast<float>(y1) + 0.75f) * k) * parameters.tangents.w);
        const float tx_min = std::min(ax, bx);
        const float tx_max = std::max(ax, bx);
        const float ty_min = std::min(ay, by);
        const float ty_max = std::max(ay, by);
        return std::pair<glm::vec3, glm::vec3>{
            glm::vec3{std::max(tx_min * z0, tx_min * z1), std::max(ty_min * z0, ty_min * z1), z0},
            glm::vec3{std::min(tx_max * z0, tx_max * z1), std::min(ty_max * z0, ty_max * z1), z1}
        };
    };
    const auto layer_middle = [&](const uint32_t layer) -> float {
        return 0.5f * (parameters.layers[layer].x + parameters.layers[layer + 1u].x);
    };
    add_pvs_test_bounds(test_case, 10, glm::vec3{-0.1f, -0.1f, 0.5f}, glm::vec3{0.1f, 0.1f, 1.5f});
    {
        const std::pair<glm::vec3, glm::vec3> box = tile_box(0, 0, 7, 7, layer_middle(2), layer_middle(2) * 1.001f);
        add_pvs_test_bounds(test_case, 11, box.first, box.second);
    }
    {
        const float z = layer_middle(1);
        const std::pair<glm::vec3, glm::vec3> box = tile_box(0, 3, 0, 3, z, z * 1.001f);
        add_pvs_test_bounds(test_case, 12, glm::vec3{-3.0f * z, box.first.y, box.first.z}, box.second);
        add_pvs_test_bounds(test_case, 13, glm::vec3{-3.0f * z, box.first.y, box.first.z}, glm::vec3{-1.5f * z, box.second.y, box.second.z});
    }
    {
        // Along tile (5, 5)'s central ray from layer 7 to past z_far.
        const float k  = static_cast<float>(c_pvs_test_tile_size);
        const float cx = parameters.tangents.x + ((5.5f * k) * parameters.tangents.y);
        const float cy = parameters.tangents.z + ((5.5f * k) * parameters.tangents.w);
        const float z0 = layer_middle(7);
        const float z1 = 30.0f;
        add_pvs_test_bounds(
            test_case, 14,
            glm::vec3{std::min(cx * z0, cx * z1), std::min(cy * z0, cy * z1), z0},
            glm::vec3{std::max(cx * z0, cx * z1), std::max(cy * z0, cy * z1), z1}
        );
    }
    {
        const float z = layer_middle(3);
        const std::pair<glm::vec3, glm::vec3> box = tile_box(7, 0, 7, 0, z, z * 1.001f);
        add_pvs_test_bounds(test_case, 15, box.first, box.second);
    }
    return test_case;
}

auto make_random_case(const uint32_t seed, const uint32_t layer_count) -> Pvs_test_case
{
    constexpr uint32_t resolution  = 64;
    constexpr uint32_t tile_size   = 4;
    const float pixel = (2.0f * std::tan(1.0471976f)) / static_cast<float>(resolution);
    Pvs_test_case test_case{
        .name       = "random",
        .parameters = make_pvs_disocclusion_parameters(
            Pvs_face_geometry{
                .resolution  = resolution,
                .tile_size   = tile_size,
                .layer_count = layer_count,
                .z_near      = 0.71f,
                .z_far       = 90.0f,
                .tan_x0      = -32.0f * pixel,
                .tan_dx      = pixel,
                .tan_y0      = 32.0f * pixel,
                .tan_dy      = -pixel,
                .cell_min    = glm::vec3{-0.45f, -0.6f, -0.3f},
                .cell_max    = glm::vec3{ 0.55f,  0.4f,  0.7f}
            },
            2048
        ),
        .layered_words = {}
    };
    reset_column_bounds(test_case, 2048);
    const std::size_t region = static_cast<std::size_t>(resolution) * resolution * layer_count;
    test_case.layered_words.resize(3u * region, 0u);
    std::fill(test_case.layered_words.begin(), test_case.layered_words.begin() + (2u * region), 0xffffffffu);
    std::mt19937 random{seed};
    std::uniform_int_distribution<uint32_t> kind_distribution{0u, 9u};
    std::uniform_int_distribution<uint32_t> id_distribution{0u, 2047u};
    std::uniform_int_distribution<uint32_t> coin{0u, 1u};
    const uint32_t tiles = resolution / tile_size;
    for (uint32_t layer = 0; layer < layer_count; ++layer) {
        for (uint32_t y = 0; y < tiles; ++y) {
            for (uint32_t x = 0; x < tiles; ++x) {
                const uint32_t kind = kind_distribution(random);
                if (kind < 5u) {
                    continue; // empty
                }
                const uint32_t id_a = id_distribution(random);
                const uint32_t id_b = id_distribution(random);
                for (uint32_t sample = 0; sample < (tile_size * tile_size); ++sample) {
                    // 5..7 full, 8..9 partial
                    if ((kind >= 8u) && (coin(random) == 0u)) {
                        continue;
                    }
                    const uint32_t px   = (x * tile_size) + (sample % tile_size);
                    const uint32_t py   = (y * tile_size) + (sample / tile_size);
                    const std::size_t w = (static_cast<std::size_t>(layer) * resolution * resolution) + (static_cast<std::size_t>(py) * resolution) + px;
                    const uint32_t low  = std::min(id_a, id_b);
                    const uint32_t high = std::max(id_a, id_b);
                    test_case.layered_words[w]                = std::bit_cast<uint32_t>(test_case.parameters.layers[layer].x);
                    test_case.layered_words[region + w]       = low;
                    test_case.layered_words[(2u * region) + w] = (coin(random) == 0u) ? low : high;
                    grow_by_sample(test_case, px, py, layer, low);
                }
            }
        }
    }
    // Surfaces crossing a layer boundary: in a few tiles the left half of
    // the samples at layer z, the right half at z + 1.
    std::uniform_int_distribution<uint32_t> tile_distribution {0u, tiles - 1u};
    std::uniform_int_distribution<uint32_t> layer_distribution{0u, layer_count - 2u};
    for (uint32_t crossing = 0; crossing < 24u; ++crossing) {
        const uint32_t x     = tile_distribution(random);
        const uint32_t y     = tile_distribution(random);
        const uint32_t layer = layer_distribution(random);
        const uint32_t id    = id_distribution(random);
        for (uint32_t sample = 0; sample < (tile_size * tile_size); ++sample) {
            const uint32_t px = (x * tile_size) + (sample % tile_size);
            const uint32_t py = (y * tile_size) + (sample / tile_size);
            for (uint32_t z = layer; z <= (layer + 1u); ++z) {
                const std::size_t w      = (static_cast<std::size_t>(z) * resolution * resolution) + (static_cast<std::size_t>(py) * resolution) + px;
                const bool        filled = ((sample % tile_size) < (tile_size / 2u)) == (z == layer);
                test_case.layered_words[w]                 = filled ? std::bit_cast<uint32_t>(test_case.parameters.layers[z].x) : 0xffffffffu;
                test_case.layered_words[region + w]        = filled ? id : 0xffffffffu;
                test_case.layered_words[(2u * region) + w] = filled ? id : 0u;
                if (filled) {
                    grow_by_sample(test_case, px, py, z, id);
                }
            }
        }
    }
    return test_case;
}

auto pvs_test_tile_index(const uint32_t x, const uint32_t y, const uint32_t layer) -> uint32_t
{
    return (((layer * c_pvs_test_tiles) + y) * c_pvs_test_tiles) + x;
}

auto has_pvs_test_entry(const Pvs_disocclusion_result& result, const uint32_t entry) -> bool
{
    return (result.column_words[entry / 32u] & (1u << (entry % 32u))) != 0u;
}

} // namespace erhe::scene_renderer::test
