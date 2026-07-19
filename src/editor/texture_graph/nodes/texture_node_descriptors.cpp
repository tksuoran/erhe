#include "texture_graph/nodes/texture_node_descriptors.hpp"

#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"
#include "erhe_texgen/value_type.hpp"

#include <fmt/format.h>

#include <array>
#include <string>

namespace editor {

using erhe::texgen::Node_descriptor;
using erhe::texgen::Input_descriptor;
using erhe::texgen::Output_descriptor;
using erhe::texgen::Parameter_descriptor;
using erhe::texgen::Parameter_kind;
using erhe::texgen::Enum_value;
using erhe::texgen::Value_type;
using erhe::texgen::Gradient_stop;
using erhe::texgen::Curve_point;
using erhe::texgen::Gradient_interpolation;

namespace {

// ---------------------------------------------------------------------------
// Small builders that keep the descriptor tables below readable. They mirror
// the fields of Material Maker's .mmg shader_model schema.
// ---------------------------------------------------------------------------

void add_input(Node_descriptor& descriptor, const char* name, Value_type type, const char* default_expression, bool function = false)
{
    Input_descriptor input{};
    input.name               = name;
    input.type               = type;
    input.default_expression = default_expression;
    input.function           = function;
    descriptor.inputs.push_back(input);
}

void add_output(Node_descriptor& descriptor, Value_type type, const char* expression)
{
    Output_descriptor output{};
    output.type       = type;
    output.expression = expression;
    descriptor.outputs.push_back(output);
}

void add_float(Node_descriptor& descriptor, const char* name, const char* label, float default_value, float min_value, float max_value, float step)
{
    Parameter_descriptor parameter{};
    parameter.name          = name;
    parameter.label         = label;
    parameter.kind          = Parameter_kind::float_parameter;
    parameter.default_float = default_value;
    parameter.min_value     = min_value;
    parameter.max_value     = max_value;
    parameter.step          = step;
    descriptor.parameters.push_back(parameter);
}

void add_color(Node_descriptor& descriptor, const char* name, const char* label, float r, float g, float b, float a)
{
    Parameter_descriptor parameter{};
    parameter.name          = name;
    parameter.label         = label;
    parameter.kind          = Parameter_kind::color_parameter;
    parameter.default_color = {r, g, b, a};
    descriptor.parameters.push_back(parameter);
}

void add_enum(Node_descriptor& descriptor, const char* name, const char* label, std::vector<Enum_value> values, std::size_t default_index)
{
    Parameter_descriptor parameter{};
    parameter.name               = name;
    parameter.label              = label;
    parameter.kind               = Parameter_kind::enum_parameter;
    parameter.enum_values        = std::move(values);
    parameter.default_enum_index = default_index;
    descriptor.parameters.push_back(parameter);
}

void add_bool(Node_descriptor& descriptor, const char* name, const char* label, bool default_value)
{
    Parameter_descriptor parameter{};
    parameter.name         = name;
    parameter.label        = label;
    parameter.kind         = Parameter_kind::bool_parameter;
    parameter.default_bool = default_value;
    descriptor.parameters.push_back(parameter);
}

void add_size(Node_descriptor& descriptor, const char* name, const char* label, int min_exponent, int max_exponent, int default_exponent)
{
    Parameter_descriptor parameter{};
    parameter.name                 = name;
    parameter.label                = label;
    parameter.kind                 = Parameter_kind::size_parameter;
    parameter.min_size_exponent    = min_exponent;
    parameter.max_size_exponent    = max_exponent;
    parameter.default_size_exponent = default_exponent;
    descriptor.parameters.push_back(parameter);
}

void add_gradient(
    Node_descriptor&                            descriptor,
    const char*                                 name,
    const char*                                 label,
    std::vector<erhe::texgen::Gradient_stop>    stops,
    erhe::texgen::Gradient_interpolation        interpolation
)
{
    Parameter_descriptor parameter{};
    parameter.name                           = name;
    parameter.label                          = label;
    parameter.kind                           = Parameter_kind::gradient_parameter;
    parameter.default_gradient_stops         = std::move(stops);
    parameter.default_gradient_interpolation = interpolation;
    descriptor.parameters.push_back(parameter);
}

void add_curve(
    Node_descriptor&                           descriptor,
    const char*                                name,
    const char*                                label,
    std::vector<erhe::texgen::Curve_point>     points
)
{
    Parameter_descriptor parameter{};
    parameter.name                 = name;
    parameter.label                = label;
    parameter.kind                 = Parameter_kind::curve_parameter;
    parameter.default_curve_points = std::move(points);
    descriptor.parameters.push_back(parameter);
}

// ---------------------------------------------------------------------------
// Generators (no inputs)
// ---------------------------------------------------------------------------

// Uniform color - ported from Material Maker uniform.mmg (MIT).
auto build_uniform() -> Node_descriptor
{
    Node_descriptor d{};
    d.name  = "uniform";
    d.label = "Uniform Color";
    d.category = "Generators";
    add_color(d, "color", "Color", 0.8f, 0.8f, 0.8f, 1.0f);
    add_output(d, Value_type::rgba, "$color");
    return d;
}

// Perlin / value noise - global ported verbatim from Material Maker perlin.mmg
// (MIT). Grayscale output over several octaves.
auto build_perlin() -> Node_descriptor
{
    Node_descriptor d{};
    d.name   = "perlin";
    d.label  = "Perlin";
    d.category = "Generators";
    d.global =
        "float perlin(vec2 uv, vec2 size, int iterations, float persistence, float seed) {\n"
        "    vec2 seed2 = rand2(vec2(seed, 1.0-seed));\n"
        "    float rv = 0.0;\n"
        "    float coef = 1.0;\n"
        "    float acc = 0.0;\n"
        "    for (int i = 0; i < iterations; ++i) {\n"
        "        vec2 step = vec2(1.0)/size;\n"
        "        vec2 xy = floor(uv*size);\n"
        "        float f0 = rand(seed2+mod(xy, size));\n"
        "        float f1 = rand(seed2+mod(xy+vec2(1.0, 0.0), size));\n"
        "        float f2 = rand(seed2+mod(xy+vec2(0.0, 1.0), size));\n"
        "        float f3 = rand(seed2+mod(xy+vec2(1.0, 1.0), size));\n"
        "        vec2 mixval = smoothstep(0.0, 1.0, fract(uv*size));\n"
        "        rv += coef * mix(mix(f0, f1, mixval.x), mix(f2, f3, mixval.x), mixval.y);\n"
        "        acc += coef;\n"
        "        size *= 2.0;\n"
        "        coef *= persistence;\n"
        "    }\n"
        "    return rv / acc;\n"
        "}\n";
    add_float(d, "scale_x",     "Scale X",     4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "scale_y",     "Scale Y",     4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "iterations",  "Octaves",     3.0f, 1.0f, 10.0f, 1.0f);
    add_float(d, "persistence", "Persistence", 0.5f, 0.0f,  1.0f, 0.05f);
    add_output(d, Value_type::grayscale, "perlin($uv, vec2($scale_x, $scale_y), int($iterations), $persistence, $seed)");
    return d;
}

// Voronoi - global ported from Material Maker voronoi.mmg (MIT), based on
// Inigo Quilez, https://www.shadertoy.com/view/ldl3W8 (MIT). Reduced from
// Material Maker's four outputs to three (distance-to-center, distance-to-edge,
// per-cell random color); the companion-node "Fill" output is dropped.
auto build_voronoi() -> Node_descriptor
{
    Node_descriptor d{};
    d.name   = "voronoi";
    d.label  = "Voronoi";
    d.category = "Generators";
    d.global =
        "// Based on https://www.shadertoy.com/view/ldl3W8 (MIT), Inigo Quilez\n"
        "vec3 iq_voronoi(vec2 x, vec2 size, vec2 stretch, float randomness, vec2 seed) {\n"
        "    vec2 n = floor(x);\n"
        "    vec2 f = fract(x);\n"
        "    vec2 mg, mr, mc;\n"
        "    float md = 8.0;\n"
        "    for (int j=-1; j<=1; j++)\n"
        "    for (int i=-1; i<=1; i++) {\n"
        "        vec2 g = vec2(float(i),float(j));\n"
        "        vec2 o = randomness*rand2(seed + mod(n + g + size, size));\n"
        "        vec2 c = g + o;\n"
        "        vec2 r = c - f;\n"
        "        vec2 rr = r*stretch;\n"
        "        float d = dot(rr,rr);\n"
        "        if (d<md) {\n"
        "            mc = c;\n"
        "            md = d;\n"
        "            mr = r;\n"
        "            mg = g;\n"
        "        }\n"
        "    }\n"
        "    md = 8.0;\n"
        "    for (int j=-2; j<=2; j++)\n"
        "    for (int i=-2; i<=2; i++) {\n"
        "        vec2 g = mg + vec2(float(i),float(j));\n"
        "        vec2 o = randomness*rand2(seed + mod(n + g + size, size));\n"
        "        vec2 r = g + o - f;\n"
        "        vec2 rr = (mr-r)*stretch;\n"
        "        if (dot(rr,rr)>0.00001)\n"
        "            md = min(md, dot(0.5*(mr+r)*stretch, normalize((r-mr)*stretch)));\n"
        "    }\n"
        "    return vec3(md, mc+n);\n"
        "}\n"
        "\n"
        "vec4 voronoi(vec2 uv, vec2 size, vec2 stretch, float intensity, float randomness, float seed) {\n"
        "    uv *= size;\n"
        "    vec3 v = iq_voronoi(uv, size, stretch, randomness, rand2(vec2(seed, 1.0-seed)));\n"
        "    return vec4(v.yz, intensity*length((uv-v.yz)*stretch), v.x);\n"
        "}\n";
    d.code = "vec4 $(name_uv)_xyzw = voronoi($uv, vec2($scale_x, $scale_y), vec2($stretch_y, $stretch_x), $intensity, $randomness, $seed);\n";
    add_float(d, "scale_x",    "Scale X",    4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "scale_y",    "Scale Y",    4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "stretch_x",  "Stretch X",  1.0f, 0.01f, 1.0f, 0.01f);
    add_float(d, "stretch_y",  "Stretch Y",  1.0f, 0.01f, 1.0f, 0.01f);
    add_float(d, "intensity",  "Intensity",  0.75f, 0.0f, 1.0f, 0.01f);
    add_float(d, "randomness", "Randomness", 1.0f, 0.0f, 1.0f, 0.01f);
    add_output(d, Value_type::grayscale, "$(name_uv)_xyzw.z");                                                    // Nodes
    add_output(d, Value_type::grayscale, "$(name_uv)_xyzw.w");                                                    // Borders
    add_output(d, Value_type::rgb,       "rand3(fract(floor($(name_uv)_xyzw.xy)/vec2($scale_x, $scale_y)))");     // Random color
    return d;
}

// Bricks - globals ported from Material Maker bricks.mmg (MIT). Reduced to the
// grayscale "Bricks pattern" output; the per-brick random-color / UV / corner
// outputs and their map inputs (mortar/bevel/round maps) are dropped, so this
// is a pure generator.
auto build_bricks() -> Node_descriptor
{
    Node_descriptor d{};
    d.name   = "bricks";
    d.label  = "Bricks";
    d.category = "Generators";
    d.global =
        "vec4 oldbrick(vec2 uv, vec2 bmin, vec2 bmax, float mortar, float round_radius, float bevel) {\n"
        "    float color;\n"
        "    vec2 size = bmax - bmin;\n"
        "    float min_size = min(size.x, size.y);\n"
        "    mortar *= min_size;\n"
        "    bevel *= min_size;\n"
        "    round_radius *= min_size;\n"
        "    vec2 center = 0.5*(bmin+bmax);\n"
        "    vec2 d = abs(uv-center)-0.5*(size)+vec2(round_radius+mortar);\n"
        "    color = length(max(d,vec2(0))) + min(max(d.x,d.y),0.0)-round_radius;\n"
        "    color = clamp(-color/bevel, 0.0, 1.0);\n"
        "    vec2 tiled_brick_pos = mod(bmin, vec2(1.0, 1.0));\n"
        "    return vec4(color, center, tiled_brick_pos.x+7.0*tiled_brick_pos.y);\n"
        "}\n"
        "\n"
        "vec4 oldbricks_rb(vec2 uv, vec2 count, float repeat, float offset) {\n"
        "    count *= repeat;\n"
        "    float x_offset = offset*step(0.5, fract(uv.y*count.y*0.5));\n"
        "    vec2 bmin = floor(vec2(uv.x*count.x-x_offset, uv.y*count.y));\n"
        "    bmin.x += x_offset;\n"
        "    bmin /= count;\n"
        "    return vec4(bmin, bmin+vec2(1.0)/count);\n"
        "}\n"
        "\n"
        "vec4 oldbricks_rb2(vec2 uv, vec2 count, float repeat, float offset) {\n"
        "    count *= repeat;\n"
        "    float x_offset = offset*step(0.5, fract(uv.y*count.y*0.5));\n"
        "    count.x = count.x*(1.0+step(0.5, fract(uv.y*count.y*0.5)));\n"
        "    vec2 bmin = floor(vec2(uv.x*count.x-x_offset, uv.y*count.y));\n"
        "    bmin.x += x_offset;\n"
        "    bmin /= count;\n"
        "    return vec4(bmin, bmin+vec2(1.0)/count);\n"
        "}\n"
        "\n"
        "vec4 oldbricks_hb(vec2 uv, vec2 count, float repeat, float offset) {\n"
        "    float pc = count.x+count.y;\n"
        "    float c = pc*repeat;\n"
        "    vec2 corner = floor(uv*c);\n"
        "    float cdiff = mod(corner.x-corner.y, pc);\n"
        "    if (cdiff < count.x) {\n"
        "        return vec4((corner-vec2(cdiff, 0.0))/c, (corner-vec2(cdiff, 0.0)+vec2(count.x, 1.0))/c);\n"
        "    } else {\n"
        "        return vec4((corner-vec2(0.0, pc-cdiff-1.0))/c, (corner-vec2(0.0, pc-cdiff-1.0)+vec2(1.0, count.y))/c);\n"
        "    }\n"
        "}\n"
        "\n"
        "vec4 oldbricks_bw(vec2 uv, vec2 count, float repeat, float offset) {\n"
        "    vec2 c = 2.0*count*repeat;\n"
        "    vec2 corner1 = floor(uv*c);\n"
        "    vec2 corner2 = count*floor(repeat*2.0*uv);\n"
        "    float cdiff = mod(dot(floor(repeat*2.0*uv), vec2(1.0)), 2.0);\n"
        "    vec2 corner;\n"
        "    vec2 size;\n"
        "    if (cdiff == 0.0) {\n"
        "        corner = vec2(corner1.x, corner2.y);\n"
        "        size = vec2(1.0, count.y);\n"
        "    } else {\n"
        "        corner = vec2(corner2.x, corner1.y);\n"
        "        size = vec2(count.x, 1.0);\n"
        "    }\n"
        "    return vec4(corner/c, (corner+size)/c);\n"
        "}\n"
        "\n"
        "vec4 oldbricks_sb(vec2 uv, vec2 count, float repeat, float offset) {\n"
        "    vec2 c = (count+vec2(1.0))*repeat;\n"
        "    vec2 corner1 = floor(uv*c);\n"
        "    vec2 corner2 = (count+vec2(1.0))*floor(repeat*uv);\n"
        "    vec2 rcorner = corner1 - corner2;\n"
        "    vec2 corner;\n"
        "    vec2 size;\n"
        "    if (rcorner.x == 0.0 && rcorner.y < count.y) {\n"
        "        corner = corner2;\n"
        "        size = vec2(1.0, count.y);\n"
        "    } else if (rcorner.y == 0.0) {\n"
        "        corner = corner2+vec2(1.0, 0.0);\n"
        "        size = vec2(count.x, 1.0);\n"
        "    } else if (rcorner.x == count.x) {\n"
        "        corner = corner2+vec2(count.x, 1.0);\n"
        "        size = vec2(1.0, count.y);\n"
        "    } else if (rcorner.y == count.y) {\n"
        "        corner = corner2+vec2(0.0, count.y);\n"
        "        size = vec2(count.x, 1.0);\n"
        "    } else {\n"
        "        corner = corner2+vec2(1.0);\n"
        "        size = vec2(count.x-1.0, count.y-1.0);\n"
        "    }\n"
        "    return vec4(corner/c, (corner+size)/c);\n"
        "}\n";
    d.code =
        "vec4 $(name_uv)_rect = oldbricks_$pattern($uv, vec2($columns, $rows), $repeat, $row_offset);\n"
        "vec4 $(name_uv)_brick = oldbrick($uv, $(name_uv)_rect.xy, $(name_uv)_rect.zw, $mortar, $round, max(0.001, $bevel));\n";
    add_enum(
        d, "pattern", "Pattern",
        {
            Enum_value{"Running Bond",   "rb"},
            Enum_value{"Running Bond 2", "rb2"},
            Enum_value{"Herringbone",    "hb"},
            Enum_value{"Basket Weave",   "bw"},
            Enum_value{"Spanish Bond",   "sb"}
        },
        0
    );
    add_float(d, "repeat",     "Repeat",  1.0f, 1.0f,  8.0f, 1.0f);
    add_float(d, "rows",       "Rows",    6.0f, 1.0f, 64.0f, 1.0f);
    add_float(d, "columns",    "Columns", 3.0f, 1.0f, 64.0f, 1.0f);
    add_float(d, "row_offset", "Offset",  0.5f, 0.0f,  1.0f, 0.01f);
    add_float(d, "mortar",     "Mortar",  0.1f, 0.0f,  0.5f, 0.01f);
    add_float(d, "bevel",      "Bevel",   0.1f, 0.0f,  0.5f, 0.01f);
    add_float(d, "round",      "Round",   0.0f, 0.0f,  0.5f, 0.01f);
    add_output(d, Value_type::grayscale, "$(name_uv)_brick.x");
    return d;
}

// Shape - globals ported from Material Maker shape.mmg (MIT). Reduced to three
// shapes (circle, polygon, star); the curved-star and rays shapes and the
// size / edge map inputs are dropped.
auto build_shape() -> Node_descriptor
{
    Node_descriptor d{};
    d.name   = "shape";
    d.label  = "Shape";
    d.category = "Generators";
    d.global =
        "float shape_circle(vec2 uv, float sides, float size, float edge) {\n"
        "    uv = 2.0*uv-1.0;\n"
        "    edge = max(edge, 1.0e-8);\n"
        "    float distance = length(uv);\n"
        "    return clamp((1.0-distance/size)/edge, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "float shape_polygon(vec2 uv, float sides, float size, float edge) {\n"
        "    uv = 2.0*uv-1.0;\n"
        "    edge = max(edge, 1.0e-8);\n"
        "    float angle = atan(uv.x, uv.y)+3.14159265359;\n"
        "    float slice = 6.28318530718/sides;\n"
        "    return clamp((1.0-(cos(floor(0.5+angle/slice)*slice-angle)*length(uv))/size)/edge, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "float shape_star(vec2 uv, float sides, float size, float edge) {\n"
        "    uv = 2.0*uv-1.0;\n"
        "    edge = max(edge, 1.0e-8);\n"
        "    float angle = atan(uv.x, uv.y);\n"
        "    float slice = 6.28318530718/sides;\n"
        "    return clamp((1.0-(cos(floor(angle*sides/6.28318530718-0.5+2.0*step(fract(angle*sides/6.28318530718), 0.5))*slice-angle)*length(uv))/size)/edge, 0.0, 1.0);\n"
        "}\n";
    add_enum(
        d, "shape", "Shape",
        {
            Enum_value{"Circle",  "circle"},
            Enum_value{"Polygon", "polygon"},
            Enum_value{"Star",    "star"}
        },
        0
    );
    add_float(d, "sides",  "Sides",  6.0f,  2.0f, 32.0f, 1.0f);
    add_float(d, "radius", "Radius", 0.85f, 0.0f,  1.0f, 0.01f);
    add_float(d, "edge",   "Edge",   0.1f,  0.0f,  1.0f, 0.01f);
    add_output(d, Value_type::grayscale, "shape_$shape($uv, $sides+1.0e-5, $radius, $edge)");
    return d;
}

// ---------------------------------------------------------------------------
// Filters (with inputs)
// ---------------------------------------------------------------------------

// Blend - globals ported from Material Maker blend.mmg (MIT). The mask input is
// dropped (opacity comes from the amount parameter only) and the "Linear Light"
// mode is dropped because Material Maker ships no blend_linear_light function.
auto build_blend() -> Node_descriptor
{
    Node_descriptor d{};
    d.name   = "blend";
    d.label  = "Blend";
    d.category = "Filters";
    d.global =
        "vec3 blend_normal(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*c1 + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "vec3 blend_dissolve(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    if (rand(uv) < opacity) { return c1; } else { return c2; }\n"
        "}\n"
        "\n"
        "vec3 blend_multiply(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*c1*c2 + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "vec3 blend_screen(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*(1.0-(1.0-c1)*(1.0-c2)) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "float blend_overlay_f(float c1, float c2) {\n"
        "    return (c1 < 0.5) ? (2.0*c1*c2) : (1.0-2.0*(1.0-c1)*(1.0-c2));\n"
        "}\n"
        "\n"
        "vec3 blend_overlay(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*vec3(blend_overlay_f(c1.x, c2.x), blend_overlay_f(c1.y, c2.y), blend_overlay_f(c1.z, c2.z)) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "vec3 blend_hard_light(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*0.5*(c1*c2+blend_overlay(uv, c1, c2, 1.0)) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "float blend_soft_light_f(float c1, float c2) {\n"
        "    return (c2 < 0.5) ? (2.0*c1*c2+c1*c1*(1.0-2.0*c2)) : 2.0*c1*(1.0-c2)+sqrt(c1)*(2.0*c2-1.0);\n"
        "}\n"
        "\n"
        "vec3 blend_soft_light(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*vec3(blend_soft_light_f(c1.x, c2.x), blend_soft_light_f(c1.y, c2.y), blend_soft_light_f(c1.z, c2.z)) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "float blend_burn_f(float c1, float c2) {\n"
        "    return (c1==0.0)?c1:max((1.0-((1.0-c2)/c1)),0.0);\n"
        "}\n"
        "\n"
        "vec3 blend_burn(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*vec3(blend_burn_f(c1.x, c2.x), blend_burn_f(c1.y, c2.y), blend_burn_f(c1.z, c2.z)) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "float blend_dodge_f(float c1, float c2) {\n"
        "    return (c1==1.0)?c1:min(c2/(1.0-c1),1.0);\n"
        "}\n"
        "\n"
        "vec3 blend_dodge(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*vec3(blend_dodge_f(c1.x, c2.x), blend_dodge_f(c1.y, c2.y), blend_dodge_f(c1.z, c2.z)) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "vec3 blend_lighten(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*max(c1, c2) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "vec3 blend_darken(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*min(c1, c2) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "vec3 blend_difference(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return opacity*clamp(c2-c1, vec3(0.0), vec3(1.0)) + (1.0-opacity)*c2;\n"
        "}\n"
        "\n"
        "vec3 blend_additive(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return c2 + c1 * opacity;\n"
        "}\n"
        "\n"
        "vec3 blend_addsub(vec2 uv, vec3 c1, vec3 c2, float opacity) {\n"
        "    return c2 + (c1 - 0.5) * 2.0 * opacity;\n"
        "}\n";
    add_input(d, "s1", Value_type::rgba, "vec4($uv.x, 1.0, 1.0, 1.0)");
    add_input(d, "s2", Value_type::rgba, "vec4(1.0, $uv.y, 1.0, 1.0)");
    add_enum(
        d, "blend_type", "Blend mode",
        {
            Enum_value{"Normal",     "normal"},
            Enum_value{"Dissolve",   "dissolve"},
            Enum_value{"Multiply",   "multiply"},
            Enum_value{"Screen",     "screen"},
            Enum_value{"Overlay",    "overlay"},
            Enum_value{"Hard Light", "hard_light"},
            Enum_value{"Soft Light", "soft_light"},
            Enum_value{"Burn",       "burn"},
            Enum_value{"Dodge",      "dodge"},
            Enum_value{"Lighten",    "lighten"},
            Enum_value{"Darken",     "darken"},
            Enum_value{"Difference", "difference"},
            Enum_value{"Additive",   "additive"},
            Enum_value{"AddSub",     "addsub"}
        },
        2 // Multiply
    );
    add_float(d, "amount", "Opacity", 0.5f, 0.0f, 1.0f, 0.01f);
    d.code =
        "vec4 $(name_uv)_s1 = $s1($uv);\n"
        "vec4 $(name_uv)_s2 = $s2($uv);\n"
        "float $(name_uv)_a = $amount;\n";
    add_output(
        d, Value_type::rgba,
        "vec4(blend_$blend_type($uv, $(name_uv)_s1.rgb, $(name_uv)_s2.rgb, $(name_uv)_a*$(name_uv)_s1.a), min(1.0, $(name_uv)_s2.a+$(name_uv)_a*$(name_uv)_s1.a))"
    );
    return d;
}

// Colorize - Material Maker colorize.mmg remaps a grayscale input through a
// gradient widget (types/gradient.gd, MIT). The gradient parameter emits a
// per-node "vec4 <fn>(float x)" and the output applies it to the grayscale
// input, exactly like Material Maker's "$gradient($input($uv))". The default
// gradient is a black@0 -> white@1 linear ramp, matching colorize.mmg.
auto build_colorize() -> Node_descriptor
{
    Node_descriptor d{};
    d.name  = "colorize";
    d.label = "Colorize";
    d.category = "Filters";
    add_input(d, "input", Value_type::grayscale, "$uv.x");
    add_gradient(
        d, "gradient", "Gradient",
        {
            Gradient_stop{.position = 0.0f, .color = {0.0f, 0.0f, 0.0f, 1.0f}},
            Gradient_stop{.position = 1.0f, .color = {1.0f, 1.0f, 1.0f, 1.0f}}
        },
        Gradient_interpolation::linear
    );
    add_output(d, Value_type::rgba, "$gradient(clamp($input($uv), 0.0, 1.0))");
    return d;
}

// Curve - a tone / levels node applying an editable Hermite curve (from
// Material Maker's curve widget, types/curve.gd, MIT) to each color channel of
// an rgba input; alpha passes through. The default curve is the identity
// 0,0 -> 1,1, so an unedited node is a no-op. The single sampled input local is
// reused across the four channel references (variant context dedups it).
auto build_curve() -> Node_descriptor
{
    Node_descriptor d{};
    d.name  = "curve";
    d.label = "Curve";
    d.category = "Filters";
    add_input(d, "in", Value_type::rgba, "vec4(vec3($uv.x), 1.0)");
    add_curve(
        d, "curve", "Curve",
        {
            Curve_point{.x = 0.0f, .y = 0.0f, .left_slope = 1.0f, .right_slope = 1.0f},
            Curve_point{.x = 1.0f, .y = 1.0f, .left_slope = 1.0f, .right_slope = 1.0f}
        }
    );
    d.code = "vec4 $(name_uv)_c = $in($uv);\n";
    add_output(
        d, Value_type::rgba,
        "vec4($curve($(name_uv)_c.r), $curve($(name_uv)_c.g), $curve($(name_uv)_c.b), $(name_uv)_c.a)"
    );
    return d;
}

// Transform - global ported from Material Maker transform.mmg (MIT). The
// optional per-channel map inputs (tx/ty/r/sx/sy) are dropped; the transform is
// driven by the scalar parameters only.
auto build_transform() -> Node_descriptor
{
    Node_descriptor d{};
    d.name   = "transform";
    d.label  = "Transform";
    d.category = "Filters";
    d.global =
        "vec2 mm_transform(vec2 uv, vec2 translate, float rotate, vec2 scale, bool repeat) {\n"
        "    vec2 rv;\n"
        "    uv -= translate;\n"
        "    uv -= vec2(0.5);\n"
        "    rv.x = cos(rotate)*uv.x + sin(rotate)*uv.y;\n"
        "    rv.y = -sin(rotate)*uv.x + cos(rotate)*uv.y;\n"
        "    rv /= scale;\n"
        "    rv += vec2(0.5);\n"
        "    if (repeat) { return fract(rv); } else { return clamp(rv, vec2(0.0), vec2(1.0)); }\n"
        "}\n";
    add_input(d, "i", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_float(d, "translate_x", "Translate X", 0.0f, -1.0f,   1.0f, 0.005f);
    add_float(d, "translate_y", "Translate Y", 0.0f, -1.0f,   1.0f, 0.005f);
    add_float(d, "rotate",      "Rotate",      0.0f, -720.0f, 720.0f, 0.5f);
    add_float(d, "scale_x",     "Scale X",     1.0f,  0.0f,  50.0f, 0.005f);
    add_float(d, "scale_y",     "Scale Y",     1.0f,  0.0f,  50.0f, 0.005f);
    add_bool (d, "repeat",      "Repeat",      false);
    add_output(
        d, Value_type::rgba,
        "$i(mm_transform($uv, vec2($translate_x, $translate_y), $rotate*0.01745329251, vec2($scale_x, $scale_y), $repeat))"
    );
    return d;
}

// Brightness / Contrast - ported from Material Maker brightness_contrast.mmg (MIT).
auto build_brightness_contrast() -> Node_descriptor
{
    Node_descriptor d{};
    d.name  = "brightness_contrast";
    d.label = "Brightness/Contrast";
    d.category = "Filters";
    add_input(d, "in", Value_type::rgba, "vec4(0.5, 0.5, 0.5, 1.0)");
    add_float(d, "brightness", "Brightness", 0.0f, -1.0f, 1.0f, 0.01f);
    add_float(d, "contrast",   "Contrast",   1.0f,  0.0f, 2.0f, 0.01f);
    add_output(
        d, Value_type::rgba,
        "vec4(clamp($in($uv).rgb*$contrast+vec3($brightness)+0.5-$contrast*0.5, vec3(0.0), vec3(1.0)), $in($uv).a)"
    );
    return d;
}

// Normal map - derivative-based normal generation ported from the edge-detect
// core of Material Maker normal_map.mmg (MIT). Material Maker's node is a
// compound graph (buffer + switch + edge-detect); this MVP flattens it to the
// edge-detect shader alone, with the height input sampled as a function-form
// input at eight offsets, and only the "Default" output format.
auto build_normal_map() -> Node_descriptor
{
    Node_descriptor d{};
    d.name   = "normal_map";
    d.label  = "Normal Map";
    d.category = "Filters";
    d.global =
        "vec3 process_normal_default(vec3 v, float multiplier) {\n"
        "    return 0.5*normalize(v.xyz*multiplier+vec3(0.0, 0.0, -1.0))+vec3(0.5);\n"
        "}\n";
    add_input(d, "in", Value_type::grayscale, "0.0", true);
    add_float(d, "amount", "Amount", 0.5f, 0.0f, 2.0f, 0.01f);
    add_size (d, "size",   "Size",   4, 12, 9);
    d.code =
        "vec3 $(name_uv)_e = vec3(1.0/$size, -1.0/$size, 0.0);\n"
        "vec2 $(name_uv)_rv = vec2(1.0, -1.0)*$in($uv+$(name_uv)_e.xy);\n"
        "$(name_uv)_rv += vec2(-1.0, 1.0)*$in($uv-$(name_uv)_e.xy);\n"
        "$(name_uv)_rv += vec2(1.0, 1.0)*$in($uv+$(name_uv)_e.xx);\n"
        "$(name_uv)_rv += vec2(-1.0, -1.0)*$in($uv-$(name_uv)_e.xx);\n"
        "$(name_uv)_rv += vec2(2.0, 0.0)*$in($uv+$(name_uv)_e.xz);\n"
        "$(name_uv)_rv += vec2(-2.0, 0.0)*$in($uv-$(name_uv)_e.xz);\n"
        "$(name_uv)_rv += vec2(0.0, 2.0)*$in($uv+$(name_uv)_e.zx);\n"
        "$(name_uv)_rv += vec2(0.0, -2.0)*$in($uv-$(name_uv)_e.zx);\n";
    add_output(d, Value_type::rgb, "process_normal_default(vec3($(name_uv)_rv, 0.0), $amount*$size/128.0)");
    return d;
}

// ---------------------------------------------------------------------------
// Phase 4b generators / noise
// ---------------------------------------------------------------------------

// FBM (Fractal Brownian Motion) - globals + octave loop ported from Material
// Maker fbm.mmg (MIT). Material Maker emits the octave accumulator as a
// per-instance helper function; here it is inlined into the code stanza (the
// descriptor model has no per-instance-function slot, only a deduplicated
// global and inline code), which is behaviorally identical. The "noise" enum
// selects the basis function (value / perlin / cellular variants).
auto build_fbm() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "fbm";
    d.label    = "FBM";
    d.category = "Generators";
    d.global =
        "float oldfbm_value(vec2 coord, vec2 size, float seed) {\n"
        "    vec2 o = floor(coord)+rand2(vec2(seed, 1.0-seed))+size;\n"
        "    vec2 f = fract(coord);\n"
        "    float p00 = rand(mod(o, size));\n"
        "    float p01 = rand(mod(o + vec2(0.0, 1.0), size));\n"
        "    float p10 = rand(mod(o + vec2(1.0, 0.0), size));\n"
        "    float p11 = rand(mod(o + vec2(1.0, 1.0), size));\n"
        "    vec2 t = f * f * (3.0 - 2.0 * f);\n"
        "    return mix(mix(p00, p10, t.x), mix(p01, p11, t.x), t.y);\n"
        "}\n"
        "\n"
        "float oldfbm_perlin(vec2 coord, vec2 size, float seed) {\n"
        "    vec2 o = floor(coord)+rand2(vec2(seed, 1.0-seed))+size;\n"
        "    vec2 f = fract(coord);\n"
        "    float a00 = rand(mod(o, size)) * 6.28318530718;\n"
        "    float a01 = rand(mod(o + vec2(0.0, 1.0), size)) * 6.28318530718;\n"
        "    float a10 = rand(mod(o + vec2(1.0, 0.0), size)) * 6.28318530718;\n"
        "    float a11 = rand(mod(o + vec2(1.0, 1.0), size)) * 6.28318530718;\n"
        "    vec2 v00 = vec2(cos(a00), sin(a00));\n"
        "    vec2 v01 = vec2(cos(a01), sin(a01));\n"
        "    vec2 v10 = vec2(cos(a10), sin(a10));\n"
        "    vec2 v11 = vec2(cos(a11), sin(a11));\n"
        "    float p00 = dot(v00, f);\n"
        "    float p01 = dot(v01, f - vec2(0.0, 1.0));\n"
        "    float p10 = dot(v10, f - vec2(1.0, 0.0));\n"
        "    float p11 = dot(v11, f - vec2(1.0, 1.0));\n"
        "    vec2 t = f * f * (3.0 - 2.0 * f);\n"
        "    return 0.5 + mix(mix(p00, p10, t.x), mix(p01, p11, t.x), t.y);\n"
        "}\n"
        "\n"
        "float oldfbm_cellular(vec2 coord, vec2 size, float seed) {\n"
        "    vec2 o = floor(coord)+rand2(vec2(seed, 1.0-seed))+size;\n"
        "    vec2 f = fract(coord);\n"
        "    float min_dist = 2.0;\n"
        "    for (float x = -1.0; x <= 1.0; x++) {\n"
        "        for (float y = -1.0; y <= 1.0; y++) {\n"
        "            vec2 node = rand2(mod(o + vec2(x, y), size)) + vec2(x, y);\n"
        "            float dist = sqrt((f - node).x * (f - node).x + (f - node).y * (f - node).y);\n"
        "            min_dist = min(min_dist, dist);\n"
        "        }\n"
        "    }\n"
        "    return min_dist;\n"
        "}\n"
        "\n"
        "float oldfbm_cellular2(vec2 coord, vec2 size, float seed) {\n"
        "    vec2 o = floor(coord)+rand2(vec2(seed, 1.0-seed))+size;\n"
        "    vec2 f = fract(coord);\n"
        "    float min_dist1 = 2.0;\n"
        "    float min_dist2 = 2.0;\n"
        "    for (float x = -1.0; x <= 1.0; x++) {\n"
        "        for (float y = -1.0; y <= 1.0; y++) {\n"
        "            vec2 node = rand2(mod(o + vec2(x, y), size)) + vec2(x, y);\n"
        "            float dist = sqrt((f - node).x * (f - node).x + (f - node).y * (f - node).y);\n"
        "            if (min_dist1 > dist) { min_dist2 = min_dist1; min_dist1 = dist; }\n"
        "            else if (min_dist2 > dist) { min_dist2 = dist; }\n"
        "        }\n"
        "    }\n"
        "    return min_dist2-min_dist1;\n"
        "}\n";
    add_enum(
        d, "noise", "Noise",
        {
            Enum_value{"Value",      "value"},
            Enum_value{"Perlin",     "perlin"},
            Enum_value{"Cellular",   "cellular"},
            Enum_value{"Cellular 2", "cellular2"}
        },
        1 // Perlin
    );
    add_float(d, "scale_x",     "Scale X",     4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "scale_y",     "Scale Y",     4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "folds",       "Folds",       0.0f, 0.0f,  5.0f, 1.0f);
    add_float(d, "iterations",  "Octaves",     5.0f, 1.0f, 10.0f, 1.0f);
    add_float(d, "persistence", "Persistence", 0.5f, 0.0f,  1.0f, 0.01f);
    d.code =
        "vec2  $(name_uv)_size = vec2($scale_x, $scale_y);\n"
        "float $(name_uv)_nf   = 0.0;\n"
        "float $(name_uv)_val  = 0.0;\n"
        "float $(name_uv)_sc   = 1.0;\n"
        "for (int $(name_uv)_i = 0; $(name_uv)_i < int($iterations); ++$(name_uv)_i) {\n"
        "    float $(name_uv)_n = oldfbm_$noise($uv*$(name_uv)_size, $(name_uv)_size, $seed);\n"
        "    for (int $(name_uv)_f = 0; $(name_uv)_f < int($folds); ++$(name_uv)_f) {\n"
        "        $(name_uv)_n = abs(2.0*$(name_uv)_n-1.0);\n"
        "    }\n"
        "    $(name_uv)_val += $(name_uv)_n * $(name_uv)_sc;\n"
        "    $(name_uv)_nf  += $(name_uv)_sc;\n"
        "    $(name_uv)_size *= 2.0;\n"
        "    $(name_uv)_sc  *= $persistence;\n"
        "}\n";
    add_output(d, Value_type::grayscale, "$(name_uv)_val / $(name_uv)_nf");
    return d;
}

// Noise (sparse black/white grid) - ported from Material Maker noise.mmg (MIT).
// The optional density map input is dropped; density comes from the parameter.
auto build_noise() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "noise";
    d.label    = "Noise";
    d.category = "Generators";
    d.code     = "vec2 $(name_uv)_uv = floor($uv*$size)/$size;\n";
    add_size (d, "size",    "Grid Size", 2, 12, 4);
    add_float(d, "density", "Density",   0.5f, 0.0f, 1.0f, 0.01f);
    add_output(d, Value_type::grayscale, "step(rand($(name_uv)_uv+vec2($seed)), $density)");
    return d;
}

// Color noise (random per-cell colors) - ported from Material Maker
// color_noise.mmg (MIT).
auto build_color_noise() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "color_noise";
    d.label    = "Color Noise";
    d.category = "Generators";
    d.global =
        "vec3 color_dots(vec2 uv, float size, float seed) {\n"
        "    vec2 seed2 = rand2(vec2(seed, 1.0-seed));\n"
        "    uv /= size;\n"
        "    vec2 point_pos = floor(uv)+vec2(0.5);\n"
        "    return rand3(seed2+point_pos);\n"
        "}\n";
    add_size(d, "size", "Grid Size", 2, 12, 4);
    add_output(d, Value_type::rgb, "color_dots($uv, 1.0/$size, $seed)");
    return d;
}

// ---------------------------------------------------------------------------
// Gradients
//
// The five gradient generators map a scalar sweep of the uv plane (linear,
// angular, radial, spiral) through a gradient widget, plus multigradient's
// randomized half-plane minimum. Ported from Material Maker gradient.mmg,
// circular_gradient.mmg, radial_gradient.mmg, spiral_gradient.mmg and
// multigradient.mmg (MIT). Default parameter values follow each .mmg's stored
// instance parameters, which is what Material Maker gives a freshly added node.
// ---------------------------------------------------------------------------

// The black@0 -> white@1 linear ramp every gradient node defaults to (the same
// default gradient colorize uses).
void add_default_gradient(Node_descriptor& descriptor)
{
    add_gradient(
        descriptor, "gradient", "Gradient",
        {
            Gradient_stop{.position = 0.0f, .color = {0.0f, 0.0f, 0.0f, 1.0f}},
            Gradient_stop{.position = 1.0f, .color = {1.0f, 1.0f, 1.0f, 1.0f}}
        },
        Gradient_interpolation::linear
    );
}

// Linear gradient - ported from Material Maker gradient.mmg (MIT). The inline
// code projects uv onto the rotated axis, normalized so the sweep still spans
// [0,1] across the unit square at any angle.
auto build_gradient() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "gradient";
    d.label    = "Gradient";
    d.category = "Gradients";
    add_float(d, "repeat", "Repeat",  1.0f,    1.0f,  32.0f, 1.0f);
    add_float(d, "rotate", "Rotate",  0.0f, -180.0f, 180.0f, 0.1f);
    add_bool (d, "mirror", "Mirror", false);
    add_default_gradient(d);
    d.code =
        "float $(name_uv)_r = 0.5+(cos($rotate*0.01745329251)*($uv.x-0.5)+sin($rotate*0.01745329251)*($uv.y-0.5))"
        "/(cos(abs(mod($rotate, 90.0)-45.0)*0.01745329251)*1.41421356237);\n";
    add_output(
        d, Value_type::rgba,
        "$gradient($mirror ? 2.0*(0.5-abs(fract($(name_uv)_r*$repeat)-0.5)) : fract($(name_uv)_r*$repeat))"
    );
    return d;
}

// Circular gradient - ported from Material Maker circular_gradient.mmg (MIT).
// The gradient sweeps with the angle around the image center.
auto build_circular_gradient() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "circular_gradient";
    d.label    = "Circular Gradient";
    d.category = "Gradients";
    add_float(d, "repeat", "Repeat",  1.0f, 1.0f, 32.0f, 1.0f);
    add_bool (d, "mirror", "Mirror", false);
    add_default_gradient(d);
    add_output(
        d, Value_type::rgba,
        "$gradient($mirror "
        "? 2.0*(0.5-abs(fract($repeat*0.15915494309*atan($uv.y-0.5, $uv.x-0.5))-0.5)) "
        ": fract($repeat*0.15915494309*atan($uv.y-0.5, $uv.x-0.5)))"
    );
    return d;
}

// Radial gradient - ported from Material Maker radial_gradient.mmg (MIT). The
// gradient sweeps with the distance from the tile center.
auto build_radial_gradient() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "radial_gradient";
    d.label    = "Radial Gradient";
    d.category = "Gradients";
    add_float(d, "repeat", "Repeat",  1.0f, 1.0f, 32.0f, 1.0f);
    add_bool (d, "mirror", "Mirror", false);
    add_default_gradient(d);
    add_output(
        d, Value_type::rgba,
        "$gradient($mirror "
        "? 2.0*(0.5-abs(fract(1.41421356237*length(fract($uv)-vec2(0.5, 0.5))*$repeat)-0.5)) "
        ": fract($repeat*1.41421356237*length(fract($uv)-vec2(0.5, 0.5))))"
    );
    return d;
}

// Spiral gradient - ported from Material Maker spiral_gradient.mmg (MIT). The
// helper is renamed from Material Maker's bare "circular_gradient" to
// mm_spiral_angle so it cannot collide with a future global of that name.
auto build_spiral_gradient() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "spiral_gradient";
    d.label    = "Spiral Gradient";
    d.category = "Gradients";
    d.global =
        "float mm_spiral_angle(vec2 uv) {\n"
        "    return fract(0.15915494309 * atan(uv.x, uv.y));\n"
        "}\n";
    add_float(d, "amount",          "Repeat",       1.0f, 0.0f, 20.0f, 1.0f);
    add_float(d, "perspective",     "Zoom",         0.3f, 0.0f, 10.0f, 0.01f);
    add_bool (d, "use_perspective", "Perspective", true);
    add_bool (d, "mirror",          "Mirror",      false);
    add_default_gradient(d);
    d.code =
        "vec2 $(name_uv)_position = $uv - 0.5;\n"
        "float $(name_uv)_length = length($(name_uv)_position);\n"
        "float $(name_uv)_spiral = (1.0 + sin("
        "($use_perspective ? (($perspective * 6.0) / $(name_uv)_length) : ($(name_uv)_length / ($perspective / 6.0)))"
        " + (mm_spiral_angle($(name_uv)_position) * 6.28318530718 * $amount))) / 2.0;\n";
    add_output(
        d, Value_type::rgba,
        "$gradient($mirror ? 2.0*(0.5-abs($(name_uv)_spiral-0.5)) : $(name_uv)_spiral)"
    );
    return d;
}

// Multigradient - ported from Material Maker multigradient.mmg (MIT). Takes the
// minimum of "count" randomly rotated linear gradients, giving a faceted
// grayscale field; the rgb input warps the sample position (xy) and offsets the
// seed (z), so it can be driven by another generator.
auto build_multigradient() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "multigradient";
    d.label    = "MultiGradient";
    d.category = "Gradients";
    d.global =
        "float multigradient(vec2 uv, int count, float seed) {\n"
        "    float rv = 1.0;\n"
        "    float angle = 0.0;\n"
        "    for (int i = 0; i < count; ++i) {\n"
        "        angle = rand(vec2(seed, angle))*6.28;\n"
        "        float v = 0.5+(cos(angle)*(uv.x-0.5)+sin(angle)*(uv.y-0.5))"
        "/(cos(abs(mod(angle, 0.5*3.141592)-0.25*3.141592))*1.41421356237);\n"
        "        rv = min(rv, v);\n"
        "    }\n"
        "    return rv;\n"
        "}\n";
    add_input(d, "in", Value_type::rgb, "vec3($uv, 0.0)");
    add_float(d, "count", "Count", 10.0f, 1.0f, 32.0f, 1.0f);
    add_output(d, Value_type::grayscale, "multigradient($in($uv).xy, int($count), float($seed)+$in($uv).z)");
    return d;
}

// Switch - ported from Material Maker's engine-level switch node
// (engine/nodes/gen_switch.gd, MIT): an input selector for A/B comparison.
//
// The selection is a COMPILE-TIME choice, matching Material Maker, whose
// _get_shader_code() delegates straight to the selected source and never looks
// at the others. Here the "source" parameter is an enum whose code fragment is
// the sample expression for one input ("$in2($uv)"), and the output expression
// is just "$source": the composer substitutes right-to-left and re-scans what
// it substituted, so the fragment expands to that input's sampling and only
// the selected branch's subtree is ever composed. The unselected branches
// contribute no globals, no uniforms and no code - which is the whole point of
// the node, since an A/B switch that still paid for B would not be a win.
//
// Consequences worth knowing: changing "source" recompiles the shader (enum
// semantics), exactly as it does in Material Maker; and a "source" pointing at
// an unconnected input yields that input's default (opaque black / 0.0) rather
// than an error.
//
// Two deviations from Material Maker, both forced by erhe's static, type-safe
// pins:
//
// - gen_switch.gd builds its pins dynamically (1-5 output groups x 2-5
//   choices); descriptors cannot, so this is a fixed 4-choice, single-output
//   switch.
// - gen_switch.gd types its pins "any". erhe's pin keys are per Value_type and
//   Graph::connect() rejects a key mismatch (Texture_pin_key), so a single
//   rgba switch could not A/B two grayscale generators - the most likely use.
//   One switch per value type is registered instead; they differ only in pin
//   type, so they share this builder.
auto build_switch(const char* name, const char* label, const Value_type type, const char* default_expression) -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = name;
    d.label    = label;
    d.category = "Utility";
    add_input(d, "in1", type, default_expression);
    add_input(d, "in2", type, default_expression);
    add_input(d, "in3", type, default_expression);
    add_input(d, "in4", type, default_expression);
    add_enum(
        d, "source", "Source",
        {
            Enum_value{.label = "Input 1", .code = "$in1($uv)"},
            Enum_value{.label = "Input 2", .code = "$in2($uv)"},
            Enum_value{.label = "Input 3", .code = "$in3($uv)"},
            Enum_value{.label = "Input 4", .code = "$in4($uv)"}
        },
        0
    );
    add_output(d, type, "$source");
    return d;
}

// ---------------------------------------------------------------------------
// Transform / UV warps
//
// Every node in this family is the same shape: sample the input at a rewritten
// coordinate, "$in(<f(uv)>)". The composer inlines the upstream subtree with
// $uv rebound to that expression, so no engine support beyond what already
// exists is needed; the two warp nodes additionally use a function-form input
// (one emitted helper, sampled several times) so the height map's subtree is
// not duplicated per sample.
//
// Ported from Material Maker rotate.mmg, scale.mmg, shear.mmg, skew.mmg,
// mirror.mmg, repeat.mmg, swirl.mmg, spherize.mmg, magnify.mmg,
// kaleidoscope.mmg, warp.mmg, directional_warp.mmg and refract.mmg (MIT).
//
// Globals here are deliberately SELF-CONTAINED, even where Material Maker
// shares one helper between nodes (its swirl calls the same rotate() as its
// rotate node). erhe deduplicates globals by exact string match, so two
// descriptors that each embedded a copy of the same helper inside a larger
// global would emit that helper twice and fail to link - and only when both
// nodes happen to be in one graph, which the standalone descriptor self-check
// cannot see. Keeping each global free of shared symbols avoids the trap.
// ---------------------------------------------------------------------------

// Rotate - ported from Material Maker rotate.mmg (MIT).
auto build_rotate() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "rotate";
    d.label    = "Rotate";
    d.category = "Transform";
    d.global =
        "vec2 mm_uv_rotate(vec2 uv, float rotate) {\n"
        "    vec2 rv;\n"
        "    rv.x = cos(rotate)*uv.x + sin(rotate)*uv.y;\n"
        "    rv.y = -sin(rotate)*uv.x + cos(rotate)*uv.y;\n"
        "    return rv;\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_float(d, "cx",     "Center X",  0.0f,   -1.0f,   1.0f, 0.005f);
    add_float(d, "cy",     "Center Y",  0.0f,   -1.0f,   1.0f, 0.005f);
    add_float(d, "rotate", "Rotate",    0.0f, -720.0f, 720.0f, 0.005f);
    d.code = "vec2 $(name_uv)_c = vec2(0.5+$cx, 0.5+$cy);\n";
    add_output(d, Value_type::rgba, "$in(mm_uv_rotate($uv-$(name_uv)_c, $rotate*0.01745329251)+$(name_uv)_c)");
    return d;
}

// Scale - ported from Material Maker scale.mmg (MIT).
auto build_scale() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "scale";
    d.label    = "Scale";
    d.category = "Transform";
    d.global =
        "vec2 mm_uv_scale(vec2 uv, vec2 center, vec2 scale) {\n"
        "    uv -= center;\n"
        "    uv /= scale;\n"
        "    uv += center;\n"
        "    return uv;\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_float(d, "cx",      "Center X", 0.0f, -1.0f,  1.0f, 0.005f);
    add_float(d, "cy",      "Center Y", 0.0f, -1.0f,  1.0f, 0.005f);
    add_float(d, "scale_x", "Scale X",  1.0f,  0.0f, 50.0f, 0.005f);
    add_float(d, "scale_y", "Scale Y",  1.0f,  0.0f, 50.0f, 0.005f);
    add_output(d, Value_type::rgba, "$in(mm_uv_scale($uv, vec2(0.5+$cx, 0.5+$cy), vec2($scale_x, $scale_y)))");
    return d;
}

// Shear - ported from Material Maker shear.mmg (MIT). The direction enum
// substitutes the two components of a vec2 mask, as it does in Material Maker.
auto build_shear() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "shear";
    d.label    = "Shear";
    d.category = "Transform";
    add_input(d, "in", Value_type::rgba, "vec4(1.0)");
    add_enum(
        d, "direction", "Direction",
        {
            Enum_value{.label = "Horizontal", .code = "1.0, 0.0"},
            Enum_value{.label = "Vertical",   .code = "0.0, 1.0"}
        },
        1
    );
    add_float(d, "amount", "Amount", 0.0f, -1.0f, 1.0f, 0.01f);
    add_float(d, "center", "Center", 0.0f,  0.0f, 1.0f, 0.01f);
    add_output(d, Value_type::rgba, "$in($uv+$amount*($uv.yx-vec2($center))*vec2($direction))");
    return d;
}

// Skew - ported from Material Maker skew.mmg (MIT). The direction enum picks
// the helper suffix (Material Maker's "uvskew_$direction" idiom).
auto build_skew() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "skew";
    d.label    = "Skew";
    d.category = "Transform";
    d.global =
        "vec2 mm_uv_skew_h(vec2 uv, float amount) {\n"
        "    return vec2(uv.x+amount*(uv.y-0.5), uv.y);\n"
        "}\n"
        "\n"
        "vec2 mm_uv_skew_v(vec2 uv, float amount) {\n"
        "    return vec2(uv.x, uv.y+amount*(uv.x-0.5));\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_enum(
        d, "direction", "Direction",
        {
            Enum_value{.label = "Horizontal", .code = "h"},
            Enum_value{.label = "Vertical",   .code = "v"}
        },
        0
    );
    add_float(d, "amount", "Amount", 0.0f, -3.0f, 3.0f, 0.005f);
    add_output(d, Value_type::rgba, "$in(mm_uv_skew_$direction($uv, $amount))");
    return d;
}

// Mirror - ported from Material Maker mirror.mmg (MIT).
auto build_mirror() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "mirror";
    d.label    = "Mirror";
    d.category = "Transform";
    d.global =
        "vec2 mm_uv_mirror_h(vec2 uv, float offset, float flip_sides) {\n"
        "    return vec2(flip_sides*max(0.0, (abs(uv.x-0.5)-0.5*offset))+0.5, uv.y);\n"
        "}\n"
        "\n"
        "vec2 mm_uv_mirror_v(vec2 uv, float offset, float flip_sides) {\n"
        "    return vec2(uv.x, flip_sides*max(0.0, (abs(uv.y-0.5)-0.5*offset))+0.5);\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_enum(
        d, "direction", "Direction",
        {
            Enum_value{.label = "Horizontal", .code = "h"},
            Enum_value{.label = "Vertical",   .code = "v"}
        },
        0
    );
    add_float(d, "offset",     "Offset",     0.0f, 0.0f, 1.0f, 0.005f);
    add_bool (d, "flip_sides", "Flip Sides", false);
    add_output(d, Value_type::rgba, "$in(mm_uv_mirror_$direction($uv, $offset, $flip_sides ? -1.0 : 1.0))");
    return d;
}

// Repeat - ported from Material Maker repeat.mmg (MIT). Tiles the input by
// wrapping the coordinate; the scale itself comes from an upstream transform.
auto build_repeat() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "repeat";
    d.label    = "Repeat";
    d.category = "Transform";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_output(d, Value_type::rgba, "$in(fract($uv))");
    return d;
}

// Swirl - ported from Material Maker swirl.mmg (MIT). The rotation is written
// out inside the helper rather than calling a shared rotate(): see the
// self-contained-globals note above.
auto build_swirl() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "swirl";
    d.label    = "Swirl";
    d.category = "Transform";
    d.global =
        "vec2 mm_uv_swirl_tile_false(vec2 uv, vec2 center, float radius, float angle) {\n"
        "    vec2 v = uv-center;\n"
        "    float l = length(v);\n"
        "    if (l > radius) {\n"
        "        return uv;\n"
        "    }\n"
        "    float a = angle*(1.0-l/radius)*(1.0-l/radius);\n"
        "    return vec2(cos(a)*v.x + sin(a)*v.y, -sin(a)*v.x + cos(a)*v.y)+center;\n"
        "}\n"
        "\n"
        "vec2 mm_uv_swirl_tile_true(vec2 uv, vec2 center, float radius, float angle) {\n"
        "    center = fract(center);\n"
        "    vec2 tile_offset = 2.0*(step(vec2(0.5), uv)-vec2(0.5));\n"
        "    uv = mm_uv_swirl_tile_false(uv, center, radius, angle);\n"
        "    uv = mm_uv_swirl_tile_false(uv, center+tile_offset, radius, angle);\n"
        "    uv = mm_uv_swirl_tile_false(uv, center+vec2(tile_offset.x, 0.0), radius, angle);\n"
        "    return mm_uv_swirl_tile_false(uv, center+vec2(0.0, tile_offset.y), radius, angle);\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_float(d, "cx",     "Center X", 0.0f,   -0.5f,   0.5f, 0.005f);
    add_float(d, "cy",     "Center Y", 0.0f,   -0.5f,   0.5f, 0.005f);
    add_float(d, "angle",  "Angle",    0.0f, -360.0f, 360.0f, 0.005f);
    add_float(d, "radius", "Radius",   0.5f,    0.0f,   0.5f, 0.01f);
    add_bool (d, "tile",   "Tile",     false);
    add_output(
        d, Value_type::rgba,
        "$in(mm_uv_swirl_tile_$tile($uv, vec2(0.5+$cx, 0.5+$cy), $radius, $angle*0.01745329251))"
    );
    return d;
}

// Spherize - ported from Material Maker spherize.mmg (MIT). Material Maker's
// two extra outputs (the inside mask and the raw r-f field) are kept.
auto build_spherize() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "spherize";
    d.label    = "Spherize";
    d.category = "Transform";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_float(d, "r",  "Radius",   0.9f,  0.0f, 1.0f, 0.01f);
    add_float(d, "a",  "Amount",   1.0f,  0.0f, 1.0f, 0.01f);
    add_float(d, "cx", "Center X", 0.0f, -1.0f, 1.0f, 0.01f);
    add_float(d, "cy", "Center Y", 0.0f, -1.0f, 1.0f, 0.01f);
    d.code =
        "vec2 $(name_uv)_co = vec2($cx+0.5, $cy+0.5);\n"
        "float $(name_uv)_f = dot(2.0*($uv - $(name_uv)_co), 2.0*($uv - $(name_uv)_co));\n";
    add_output(
        d, Value_type::rgba,
        "mix($in($(name_uv)_co-($(name_uv)_co-$uv)/(sqrt(abs($r-$(name_uv)_f))*max($a, 0.0)+1.0)), $in($uv), step($r, $(name_uv)_f))"
    );
    add_output(d, Value_type::grayscale, "step($(name_uv)_f, $r)");
    add_output(d, Value_type::grayscale, "$r-$(name_uv)_f");
    return d;
}

// Magnify - ported from Material Maker magnify.mmg (MIT). The grayscale "s"
// input modulates the magnification per pixel.
auto build_magnify() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "magnify";
    d.label    = "Magnify";
    d.category = "Transform";
    add_input(d, "in", Value_type::rgba,      "vec4($uv, 0.0, 1.0)");
    add_input(d, "s",  Value_type::grayscale, "max(1.0-2.0*length($uv-vec2(0.5)), 0.0)");
    add_float(d, "cx",    "Center X", 0.0f, -1.0f,  1.0f, 0.01f);
    add_float(d, "cy",    "Center Y", 0.0f, -1.0f,  1.0f, 0.01f);
    add_float(d, "scale", "Scale",    1.0f,  0.0f, 10.0f, 0.01f);
    add_output(
        d, Value_type::rgba,
        "$in(vec2($cx+0.5, $cy+0.5)+($uv-vec2($cx+0.5, $cy+0.5))/mix(1.0, $scale, $s($uv-vec2($cx, $cy))))"
    );
    return d;
}

// Kaleidoscope - ported from Material Maker kaleidoscope.mmg (MIT). Material
// Maker's "variations" option is dropped: it feeds the per-sector index into
// its input-variation machinery, which erhe's composer has no equivalent for.
// The helper still returns the sector index in .z so it can be revisited.
auto build_kaleidoscope() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "kaleidoscope";
    d.label    = "Kaleidoscope";
    d.category = "Transform";
    d.global =
        "vec3 mm_uv_kaleidoscope(vec2 uv, float count, float offset, float seed) {\n"
        "    float pi = 3.14159265359;\n"
        "    offset *= pi/180.0;\n"
        "    offset += pi*(1.0/count+0.5);\n"
        "    uv -= vec2(0.5);\n"
        "    float l = length(uv);\n"
        "    float angle = atan(uv.y, uv.x)+offset;\n"
        "    angle += (1.0-sign(angle))*pi;\n"
        "    float a = mod(angle, 2.0*pi/count)-offset;\n"
        "    return vec3(vec2(0.5)+l*vec2(cos(a), sin(a)), rand(vec2(seed))+floor(0.5*angle*count/pi));\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)", true);
    add_float(d, "count",  "Count",  4.0f,    2.0f,  10.0f, 1.0f);
    add_float(d, "offset", "Offset", 0.0f, -180.0f, 180.0f, 0.1f);
    d.code = "vec3 $(name_uv)_kal = mm_uv_kaleidoscope($uv, $count, $offset, $seed);\n";
    add_output(d, Value_type::rgba, "$in($(name_uv)_kal.xy)");
    return d;
}

// Warp - ported from Material Maker warp.mmg (MIT). Displaces the input along
// the slope of a height map: the noise -> warp -> colorize workflow.
//
// Material Maker emits the slope as a per-node "instance" helper
// ($(name)_slope); erhe descriptors have no instance stanza, so the same
// central-difference body is written inline in the code stanza. It is
// evaluated once per (node, uv) variant, which is where the helper's only
// benefit lay. The height map input is function-form so its subtree is emitted
// once and called four times rather than being inlined four times.
auto build_warp() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "warp";
    d.label    = "Warp";
    d.category = "Transform";
    add_input(d, "in",           Value_type::rgba,      "vec4(sin($uv.x*20.0)*0.5+0.5, sin($uv.y*20.0)*0.5+0.5, 0.0, 1.0)");
    add_input(d, "d",            Value_type::grayscale, "0.0", true);
    add_input(d, "strength_map", Value_type::grayscale, "1.0");
    add_enum(
        d, "mode", "Mode",
        {
            Enum_value{.label = "Slope",           .code = "$(name_uv)_slope"},
            Enum_value{.label = "Distance to top", .code = "$(name_uv)_slope*(1.0-$d($uv))"}
        },
        0
    );
    add_float(d, "amount", "Amount",  0.0f, 0.0f,  1.0f, 0.005f);
    add_float(d, "eps",    "Epsilon", 0.05f, 0.005f, 0.2f, 0.005f);
    d.code =
        "vec2 $(name_uv)_slope = vec2("
        "$d(fract($uv+vec2($eps, 0.0)))-$d(fract($uv-vec2($eps, 0.0))), "
        "$d(fract($uv+vec2(0.0, $eps)))-$d(fract($uv-vec2(0.0, $eps))));\n"
        "vec2 $(name_uv)_warp = $mode;\n";
    add_output(d, Value_type::rgba, "$in($uv+$amount*$strength_map($uv)*$(name_uv)_warp)");
    return d;
}

// Directional warp - ported from Material Maker directional_warp.mmg (MIT).
// Displaces along a fixed angle, modulated by two grayscale maps.
auto build_directional_warp() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "directional_warp";
    d.label    = "Directional Warp";
    d.category = "Transform";
    add_input(d, "in",          Value_type::rgba,      "vec4($uv, 0.0, 1.0)");
    add_input(d, "anglemap",    Value_type::grayscale, "1.0");
    add_input(d, "strengthmap", Value_type::grayscale, "1.0");
    add_float(d, "angle",    "Angle",    0.0f, -180.0f, 180.0f, 0.1f);
    add_float(d, "strength", "Strength", 0.1f,   -1.0f,   1.0f, 0.005f);
    add_output(
        d, Value_type::rgba,
        "$in($uv + vec2(cos($angle*$anglemap($uv)*0.01745329251), sin($angle*$anglemap($uv)*0.01745329251))"
        "*($strengthmap($uv)-0.5)*$strength)"
    );
    return d;
}

// Refract - ported from Material Maker refract.mmg (MIT). Offsets the input as
// if seen through a surface whose height is the "s" map. As with warp,
// Material Maker's per-node instance helper is written inline instead.
auto build_refract() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "refract";
    d.label    = "Refract";
    d.category = "Transform";
    add_input(d, "in", Value_type::rgba,      "vec4($uv, 0.0, 1.0)");
    add_input(d, "s",  Value_type::grayscale, "max(1.0-2.0*length($uv-vec2(0.5)), 0.0)", true);
    add_float(d, "refract", "Refraction", 1.0f, 0.0f, 2.0f, 0.01f);
    d.code =
        "vec2 $(name_uv)_eps = vec2(0.001, 0.0);\n"
        "vec3 $(name_uv)_n = normalize(vec3("
        "$s($uv+$(name_uv)_eps)-$s($uv-$(name_uv)_eps), "
        "$s($uv+$(name_uv)_eps.yx)-$s($uv-$(name_uv)_eps.yx), "
        "-10.0*$(name_uv)_eps.x));\n"
        "float $(name_uv)_h = $s($uv);\n"
        "vec3 $(name_uv)_i = vec3(0.0, 0.0, -1.0);\n"
        "float $(name_uv)_mu = 1.0/max($refract, 0.0001);\n"
        "float $(name_uv)_dot = dot($(name_uv)_n, $(name_uv)_i);\n"
        "vec3 $(name_uv)_t = sqrt(max(0.0, 1.0-$(name_uv)_mu*$(name_uv)_mu*(1.0-$(name_uv)_dot*$(name_uv)_dot)))"
        "*$(name_uv)_n+$(name_uv)_mu*($(name_uv)_i-$(name_uv)_dot*$(name_uv)_n);\n";
    add_output(d, Value_type::rgba, "$in($uv+$(name_uv)_h*$(name_uv)_t.xy/$(name_uv)_t.z)");
    return d;
}

// ---------------------------------------------------------------------------
// Color / tone filters
//
// Ported from Material Maker greyscale.mmg, tones.mmg, tones_map.mmg,
// tones_range.mmg, tones_step.mmg, tonality.mmg, convert_colorspace.mmg,
// colormap.mmg, palettize.mmg, default_color.mmg, compare.mmg,
// ensure_greyscale.mmg, ensure_rgba.mmg and uniform_greyscale.mmg (MIT).
//
// Input types differ from Material Maker in one systematic way. Material Maker
// relies on automatic conversion at connection time, so it can type an input
// "rgb" and still accept anything; erhe's pin keys are per Value_type and
// Graph::connect() refuses a mismatch, so an input's declared type is exactly
// what it accepts. Inputs here are therefore typed to the value type their
// producers actually emit (usually rgba), with the conversion written into the
// expression. The two "ensure" nodes are re-purposed accordingly: in Material
// Maker they are no-op coercions, and here they are the explicit bridges that
// type-strict pins make necessary (rgba -> f and f -> rgba), using the same
// conversion expressions as erhe::texgen::convert().
//
// Not ported: auto_tones (it has no shader_model of its own - it needs a
// min/max reduction pass over the whole image, which the composer cannot
// express) and colorspace_roundtrip (a test fixture).
// ---------------------------------------------------------------------------

// Greyscale - ported from Material Maker greyscale.mmg (MIT). Five weightings,
// picked by an enum that names the helper (Material Maker's "gs_$mode" idiom).
auto build_greyscale() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "greyscale";
    d.label    = "Greyscale";
    d.category = "Color";
    d.global =
        "float gs_min(vec3 c) {\n"
        "    return min(c.r, min(c.g, c.b));\n"
        "}\n"
        "\n"
        "float gs_max(vec3 c) {\n"
        "    return max(c.r, max(c.g, c.b));\n"
        "}\n"
        "\n"
        "float gs_lightness(vec3 c) {\n"
        "    return 0.5*(max(c.r, max(c.g, c.b)) + min(c.r, min(c.g, c.b)));\n"
        "}\n"
        "\n"
        "float gs_average(vec3 c) {\n"
        "    return 0.333333333333*(c.r + c.g + c.b);\n"
        "}\n"
        "\n"
        "float gs_luminosity(vec3 c) {\n"
        "    return 0.21 * c.r + 0.72 * c.g + 0.07 * c.b;\n"
        "}\n";
    // Material Maker types this input "rgb" and relies on automatic conversion
    // at connection time. erhe pins are type-strict, and nearly every color
    // producer here emits rgba (uniform, colorize, blend, the gradients, the
    // tone filters), so an rgb input would refuse the very sources this node
    // exists to desaturate. It takes rgba and uses .rgb; ensure_greyscale is
    // the rgb -> f bridge for the few rgb producers (color_noise, voronoi's
    // color output, normal_map).
    add_input(d, "in", Value_type::rgba, "vec4(vec3($uv.x), 1.0)");
    add_enum(
        d, "mode", "Mode",
        {
            Enum_value{.label = "Lightness",  .code = "lightness"},
            Enum_value{.label = "Average",    .code = "average"},
            Enum_value{.label = "Luminosity", .code = "luminosity"},
            Enum_value{.label = "Min",        .code = "min"},
            Enum_value{.label = "Max",        .code = "max"}
        },
        2
    );
    add_output(d, Value_type::grayscale, "gs_$mode(($in($uv)).rgb)");
    return d;
}

// Ensure Greyscale - Material Maker's ensure_greyscale.mmg (MIT) is a no-op
// coercion relying on automatic conversion. Here it is the explicit rgb -> f
// bridge that type-strict pins require, for the rgb producers that greyscale
// (rgba) cannot take: color_noise, voronoi's color output, normal_map. The
// expression is the same average erhe::texgen::convert() uses for rgb -> f.
auto build_ensure_greyscale() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "ensure_greyscale";
    d.label    = "RGB To Greyscale";
    d.category = "Channels";
    add_input(d, "in", Value_type::rgb, "vec3($uv.x)");
    add_output(d, Value_type::grayscale, "(dot($in($uv), vec3(1.0))/3.0)");
    return d;
}

// Ensure RGBA - the f -> rgba counterpart of the above (Material Maker's
// ensure_rgba.mmg is likewise a no-op coercion).
auto build_ensure_rgba() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "ensure_rgba";
    d.label    = "To RGBA";
    d.category = "Channels";
    add_input(d, "in", Value_type::grayscale, "$uv.x");
    add_output(d, Value_type::rgba, "vec4(vec3($in($uv)), 1.0)");
    return d;
}

// Uniform Greyscale - ported from Material Maker uniform_greyscale.mmg (MIT).
// The grayscale counterpart of the existing uniform color node.
auto build_uniform_greyscale() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "uniform_greyscale";
    d.label    = "Uniform Greyscale";
    d.category = "Generators";
    add_float(d, "color", "Value", 0.5f, 0.0f, 1.0f, 0.01f);
    add_output(d, Value_type::grayscale, "$color");
    return d;
}

// Default Color - ported from Material Maker default_color.mmg (MIT). Passes
// its input through, substituting a chosen color while it is unconnected.
auto build_default_color() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "default_color";
    d.label    = "Default Color";
    d.category = "Color";
    add_input(d, "in", Value_type::rgba, "$default");
    add_color(d, "default", "Default", 1.0f, 1.0f, 1.0f, 1.0f);
    add_output(d, Value_type::rgba, "$in($uv)");
    return d;
}

// Compare - ported from Material Maker compare.mmg (MIT). Absolute difference
// of two inputs, summed over the channels; the A/B diff companion to Switch.
auto build_compare() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "compare";
    d.label    = "Compare";
    d.category = "Color";
    add_input(d, "in1", Value_type::rgba, "vec4(0.0)");
    add_input(d, "in2", Value_type::rgba, "vec4(0.0)");
    add_output(d, Value_type::grayscale, "dot(abs($in1($uv)-$in2($uv)), vec4(1.0))");
    return d;
}

// Tones (levels) - ported from Material Maker tones.mmg (MIT). A full levels
// control: per-channel input black / mid / white and output black / white.
auto build_tones() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "tones";
    d.label    = "Tones (Levels)";
    d.category = "Color";
    d.global =
        "vec4 adjust_levels(vec4 color, vec4 in_min, vec4 in_mid, vec4 in_max, vec4 out_min, vec4 out_max) {\n"
        "    color = clamp((color-in_min)/(in_max-in_min), 0.0, 1.0);\n"
        "    in_mid = (in_mid-in_min)/(in_max-in_min);\n"
        "    vec4 dark = step(in_mid, color);\n"
        "    color = 0.5*mix(color/(in_mid), 1.0+(color-in_mid)/(1.0-in_mid), dark);\n"
        "    return out_min+color*(out_max-out_min);\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4(vec3($uv.x), 1.0)");
    add_color(d, "in_min",  "In Black",  0.0f,      0.0f,      0.0f,      0.0f);
    add_color(d, "in_mid",  "In Mid",    0.498039f, 0.498039f, 0.498039f, 0.498039f);
    add_color(d, "in_max",  "In White",  1.0f,      1.0f,      1.0f,      1.0f);
    add_color(d, "out_min", "Out Black", 0.0f,      0.0f,      0.0f,      1.0f);
    add_color(d, "out_max", "Out White", 1.0f,      1.0f,      1.0f,      1.0f);
    add_output(d, Value_type::rgba, "adjust_levels($in($uv), $in_min, $in_mid, $in_max, $out_min, $out_max)");
    return d;
}

// Tones Map - ported from Material Maker tones_map.mmg (MIT). Linear remap of
// one input range onto an output range; alpha passes through.
auto build_tones_map() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "tones_map";
    d.label    = "Tones Map";
    d.category = "Color";
    add_input(d, "in", Value_type::rgba, "vec4(0.5, 0.5, 0.5, 1.0)");
    add_float(d, "in_min",  "In Min",  0.0f, 0.0f, 1.0f, 0.01f);
    add_float(d, "in_max",  "In Max",  1.0f, 0.0f, 1.0f, 0.01f);
    add_float(d, "out_min", "Out Min", 0.0f, 0.0f, 1.0f, 0.01f);
    add_float(d, "out_max", "Out Max", 1.0f, 0.0f, 1.0f, 0.01f);
    d.code = "vec4 $(name_uv)_c = $in($uv);\n";
    add_output(
        d, Value_type::rgba,
        "vec4(vec3($out_min)+($(name_uv)_c.rgb-vec3($in_min))*vec3(($out_max-($out_min))/max(0.0001, $in_max-($in_min))), $(name_uv)_c.a)"
    );
    return d;
}

// Tones Range - ported from Material Maker tones_range.mmg (MIT). Isolates a
// band of values into a mask. The invert boolean selects between two locals by
// name ("$(name_uv)_$invert"), Material Maker's idiom for a branch-free toggle.
auto build_tones_range() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "tones_range";
    d.label    = "Tones Range";
    d.category = "Color";
    add_input(d, "in", Value_type::grayscale, "($uv.x + $uv.y) / 2.0");
    add_float(d, "value",    "Value",    0.5f,  0.0f, 1.0f, 0.01f);
    add_float(d, "width",    "Width",    0.25f, 0.0f, 1.0f, 0.01f);
    add_float(d, "contrast", "Contrast", 0.5f,  0.0f, 1.0f, 0.01f);
    add_bool (d, "invert",   "Invert",   false);
    d.code =
        "float $(name_uv)_step = clamp(($in($uv) - ($value))/max(0.0001, $width)+0.5, 0.0, 1.0);\n"
        "float $(name_uv)_false = clamp((min($(name_uv)_step, 1.0-$(name_uv)_step) * 2.0) / max(0.0001, 1.0 - $contrast), 0.0, 1.0);\n"
        "float $(name_uv)_true = 1.0-$(name_uv)_false;\n";
    add_output(d, Value_type::grayscale, "$(name_uv)_$invert");
    return d;
}

// Tones Step - ported from Material Maker tones_step.mmg (MIT). A soft
// threshold; same name-selected-local idiom as tones_range.
auto build_tones_step() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "tones_step";
    d.label    = "Tones Step";
    d.category = "Color";
    add_input(d, "in", Value_type::rgba, "vec4(0.5, 0.5, 0.5, 1.0)");
    add_float(d, "value",  "Value",  0.5f, 0.0f, 1.0f, 0.01f);
    add_float(d, "width",  "Width",  1.0f, 0.0f, 1.0f, 0.01f);
    add_bool (d, "invert", "Invert", false);
    d.code =
        "vec4 $(name_uv)_in = $in($uv);\n"
        "vec3 $(name_uv)_false = clamp(($(name_uv)_in.rgb-vec3($value))/max(0.0001, $width)+vec3(0.5), vec3(0.0), vec3(1.0));\n"
        "vec3 $(name_uv)_true = vec3(1.0)-$(name_uv)_false;\n";
    add_output(d, Value_type::rgba, "vec4($(name_uv)_$invert, $(name_uv)_in.a)");
    return d;
}

// Tonality - ported from Material Maker tonality.mmg (MIT). A tone curve on a
// grayscale input (the existing "curve" node is its per-RGB-channel sibling).
auto build_tonality() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "tonality";
    d.label    = "Tonality";
    d.category = "Color";
    add_input(d, "in", Value_type::grayscale, "$uv.x");
    add_curve(
        d, "curve", "Curve",
        {
            Curve_point{.x = 0.0f, .y = 0.0f, .left_slope = 0.0f, .right_slope = 1.0f},
            Curve_point{.x = 1.0f, .y = 1.0f, .left_slope = 1.0f, .right_slope = 0.0f}
        }
    );
    add_output(d, Value_type::grayscale, "$curve($in($uv))");
    return d;
}

// Convert Colorspace - ported from Material Maker convert_colorspace.mmg (MIT).
// The two enums together name the conversion helper ("$(direction)_$(colorspace)").
// The helper names are Material Maker's and are distinct from adjust_hsv's
// rgb_to_hsv / hsv_to_rgb, so the two nodes' globals cannot collide.
auto build_convert_colorspace() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "convert_colorspace";
    d.label    = "Convert Colorspace";
    d.category = "Color";
    d.global =
        "vec3 from_rgb_to_rgb(vec3 c) { return c; }\n"
        "vec3 to_rgb_from_rgb(vec3 c) { return c; }\n"
        "\n"
        "vec3 from_rgb_to_hsv(vec3 c) {\n"
        "    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
        "    vec4 p = c.g < c.b ? vec4(c.bg, K.wz) : vec4(c.gb, K.xy);\n"
        "    vec4 q = c.r < p.x ? vec4(p.xyw, c.r) : vec4(c.r, p.yzx);\n"
        "    float d = q.x - min(q.w, q.y);\n"
        "    float e = 1.0e-10;\n"
        "    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);\n"
        "}\n"
        "\n"
        "vec3 to_rgb_from_hsv(vec3 c) {\n"
        "    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
        "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
        "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
        "}\n"
        "\n"
        "// Matrix coefficients taken from https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.709_conversion\n"
        "vec3 from_rgb_to_yuv(vec3 c) {\n"
        "    return c * mat3(\n"
        "        vec3( 0.2126,  0.7152,  0.0722),\n"
        "        vec3(-0.1146, -0.3854,  0.5),\n"
        "        vec3( 0.5,    -0.4542, -0.0458)\n"
        "    );\n"
        "}\n"
        "\n"
        "vec3 to_rgb_from_yuv(vec3 c) {\n"
        "    return c * mat3(\n"
        "        vec3(1.0,  0.0,     1.5748),\n"
        "        vec3(1.0, -0.1873, -0.4681),\n"
        "        vec3(1.0,  1.8556,  0.0)\n"
        "    );\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4(1.0)");
    add_enum(
        d, "direction", "Direction",
        {
            Enum_value{.label = "Convert from RGB to", .code = "from_rgb_to"},
            Enum_value{.label = "Convert to RGB from", .code = "to_rgb_from"}
        },
        0
    );
    add_enum(
        d, "colorspace", "Colorspace",
        {
            Enum_value{.label = "RGB (no-op)", .code = "rgb"},
            Enum_value{.label = "HSV",         .code = "hsv"},
            Enum_value{.label = "YUV",         .code = "yuv"}
        },
        1
    );
    d.code = "vec4 $(name_uv)_c = $in($uv);\n";
    add_output(d, Value_type::rgba, "vec4($(direction)_$(colorspace)($(name_uv)_c.rgb), $(name_uv)_c.a)");
    return d;
}

// Colormap - ported from Material Maker colormap.mmg (MIT). Uses a grayscale
// input as a lookup coordinate into a second (2D) input, sampling it along a
// row or a column. The direction enum supplies the whole lookup coordinate
// expression, which the composer re-scans - so "$input" and "$offset" inside
// the enum fragment resolve normally.
auto build_colormap() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "colormap";
    d.label    = "Colormap";
    d.category = "Color";
    add_input(d, "in",       Value_type::grayscale, "$uv.x");
    add_input(d, "colormap", Value_type::rgba,      "vec4(vec3($uv.x), 1.0)");
    add_enum(
        d, "direction", "Direction",
        {
            Enum_value{.label = "Horizontal", .code = "vec2($in($uv), $offset)"},
            Enum_value{.label = "Vertical",   .code = "vec2($offset, $in($uv))"}
        },
        0
    );
    add_float(d, "offset", "Offset", 0.5f, 0.0f, 1.0f, 0.01f);
    add_output(d, Value_type::rgba, "$colormap($direction)");
    return d;
}

// Palettize - ported from Material Maker palettize.mmg (MIT). Snaps each pixel
// to the nearest color found in a size x size sampling of the palette input.
//
// Material Maker emits the search as a per-node "instance" helper; erhe
// descriptors have no instance stanza, so the loop is written into the code
// stanza (the same approach warp and refract use). The palette input is
// function-form, so its subtree is emitted once and called from inside the
// loop rather than being inlined size*size times.
auto build_palettize() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "palettize";
    d.label    = "Palettize";
    d.category = "Color";
    add_input(d, "in",      Value_type::rgba, "vec4(vec3($uv.x), 1.0)");
    add_input(d, "palette", Value_type::rgba, "vec4($uv.x, $uv.y, 0.0, 1.0)", true);
    add_float(d, "size", "Palette Size", 8.0f, 1.0f, 32.0f, 1.0f);
    d.code =
        "vec4 $(name_uv)_in = $in($uv);\n"
        "int $(name_uv)_size = int(clamp($size, 1.0, 32.0));\n"
        "float $(name_uv)_min_dist = 10.0;\n"
        "vec3 $(name_uv)_best = vec3(0.0);\n"
        "for (int i = 0; i < $(name_uv)_size; ++i) {\n"
        "    float px = (float(i)+0.5)/float($(name_uv)_size);\n"
        "    for (int j = 0; j < $(name_uv)_size; ++j) {\n"
        "        float py = (float(j)+0.5)/float($(name_uv)_size);\n"
        "        vec3 pc = ($palette(vec2(px, py))).rgb;\n"
        "        float pl = length($(name_uv)_in.rgb - pc);\n"
        "        if (pl < $(name_uv)_min_dist) {\n"
        "            $(name_uv)_min_dist = pl;\n"
        "            $(name_uv)_best = pc;\n"
        "        }\n"
        "    }\n"
        "}\n";
    add_output(d, Value_type::rgba, "vec4($(name_uv)_best, $(name_uv)_in.a)");
    return d;
}

// ---------------------------------------------------------------------------
// Phase 4b patterns
// ---------------------------------------------------------------------------

// Sine wave - ported from Material Maker sine_wave.mmg (MIT).
auto build_sine_wave() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "sine_wave";
    d.label    = "Sine Wave";
    d.category = "Patterns";
    add_float(d, "amplitude", "Amplitude", 0.5f, 0.0f,  1.0f, 0.01f);
    add_float(d, "frequency", "Frequency", 1.0f, 0.0f, 16.0f, 1.0f);
    add_float(d, "phase",     "Phase",     0.0f, 0.0f,  1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "1.0-abs(2.0*($uv.y-0.5)-$amplitude*sin(($frequency*$uv.x+$phase)*6.28318530718))"
    );
    return d;
}

// Truchet - globals ported from Material Maker truchet.mmg (MIT). The "shape"
// enum picks the tile primitive (line / circle).
auto build_truchet() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "truchet";
    d.label    = "Truchet";
    d.category = "Patterns";
    d.global =
        "float truchet1(vec2 uv, vec2 seed) {\n"
        "    vec2 i = floor(uv);\n"
        "    vec2 f = fract(uv)-vec2(0.5);\n"
        "    return 1.0-abs(abs((2.0*step(rand(i+seed), 0.5)-1.0)*f.x+f.y)-0.5);\n"
        "}\n"
        "\n"
        "float truchet2(vec2 uv, vec2 seed) {\n"
        "    vec2 i = floor(uv);\n"
        "    vec2 f = fract(uv);\n"
        "    float random = step(rand(i+seed), 0.5);\n"
        "    f.x *= 2.0*random-1.0;\n"
        "    f.x += 1.0-random;\n"
        "    return 1.0-min(abs(length(f)-0.5), abs(length(1.0-f)-0.5));\n"
        "}\n";
    add_enum(
        d, "shape", "Shape",
        {
            Enum_value{"Line",   "1"},
            Enum_value{"Circle", "2"}
        },
        0
    );
    add_float(d, "size", "Size", 4.0f, 2.0f, 64.0f, 1.0f);
    add_output(d, Value_type::grayscale, "truchet$shape($uv*$size, vec2(float($seed)))");
    return d;
}

// Weave - global ported from Material Maker weave.mmg (MIT). The optional width
// map input is dropped; the width comes from the parameter only.
auto build_weave() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "weave";
    d.label    = "Weave";
    d.category = "Patterns";
    d.global =
        "float oldweave(vec2 uv, vec2 count, float width) {\n"
        "    uv *= count;\n"
        "    float c = (sin(3.1415926*(uv.x+floor(uv.y)))*0.5+0.5)*step(abs(fract(uv.y)-0.5), width*0.5);\n"
        "    c = max(c, (sin(3.1415926*(1.0+uv.y+floor(uv.x)))*0.5+0.5)*step(abs(fract(uv.x)-0.5), width*0.5));\n"
        "    return c;\n"
        "}\n";
    add_float(d, "columns", "Size X", 4.0f, 2.0f, 32.0f, 1.0f);
    add_float(d, "rows",    "Size Y", 4.0f, 2.0f, 32.0f, 1.0f);
    add_float(d, "width",   "Width",  0.8f, 0.0f,  1.0f, 0.05f);
    add_output(d, Value_type::grayscale, "oldweave($uv, vec2($columns, $rows), $width)");
    return d;
}

// ---------------------------------------------------------------------------
// Phase 4b filters / math
// ---------------------------------------------------------------------------

// Math - the two-operand math node ported from Material Maker math.mmg (MIT).
// The "op" enum substitutes a full GLSL expression fragment (which itself
// samples the two inputs); the "clamp" boolean picks between the raw and the
// clamped result local, exactly like Material Maker.
auto build_math() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "math";
    d.label    = "Math";
    d.category = "Filters";
    d.global =
        "float pingpong(float a, float b) {\n"
        "    return (b != 0.0) ? abs(fract((a - b) / (b * 2.0)) * b * 2.0 - b) : 0.0;\n"
        "}\n";
    add_input(d, "in1", Value_type::grayscale, "$default_in1");
    add_input(d, "in2", Value_type::grayscale, "$default_in2");
    add_enum(
        d, "op", "Operation",
        {
            Enum_value{"A+B",               "$in1($uv)+$in2($uv)"},
            Enum_value{"A-B",               "$in1($uv)-$in2($uv)"},
            Enum_value{"A*B",               "$in1($uv)*$in2($uv)"},
            Enum_value{"A/B",               "$in1($uv)/$in2($uv)"},
            Enum_value{"pow(A, B)",         "pow($in1($uv),$in2($uv))"},
            Enum_value{"log(A)",            "log($in1($uv))"},
            Enum_value{"abs(A)",            "abs($in1($uv))"},
            Enum_value{"floor(A)",          "floor($in1($uv))"},
            Enum_value{"fract(A)",          "fract($in1($uv))"},
            Enum_value{"min(A, B)",         "min($in1($uv),$in2($uv))"},
            Enum_value{"max(A, B)",         "max($in1($uv),$in2($uv))"},
            Enum_value{"A<B",               "(1.0-step($in2($uv),$in1($uv)))"},
            Enum_value{"sin(A*B)",          "sin($in1($uv)*$in2($uv))"},
            Enum_value{"cos(A*B)",          "cos($in1($uv)*$in2($uv))"},
            Enum_value{"smoothstep(0,1,A)", "smoothstep(0.0, 1.0, $in1($uv))"},
            Enum_value{"ping-pong(A, B)",   "pingpong($in1($uv),$in2($uv))"},
            Enum_value{"mod(A, B)",         "mod($in1($uv), $in2($uv))"},
            Enum_value{"sqrt(A)",           "sqrt($in1($uv))"}
        },
        0
    );
    add_float(d, "default_in1", "Default A", 0.0f, 0.0f, 1.0f, 0.01f);
    add_float(d, "default_in2", "Default B", 0.0f, 0.0f, 1.0f, 0.01f);
    add_bool (d, "clamp",       "Clamp",     false);
    d.code =
        "float $(name_uv)_clamp_false = $op;\n"
        "float $(name_uv)_clamp_true = clamp($(name_uv)_clamp_false, 0.0, 1.0);\n";
    add_output(d, Value_type::grayscale, "$(name_uv)_clamp_$clamp");
    return d;
}

// Invert - ported from Material Maker invert.mmg (MIT). Inverts RGB, keeps A.
auto build_invert() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "invert";
    d.label    = "Invert";
    d.category = "Filters";
    add_input(d, "in", Value_type::rgba, "vec4(1.0, 1.0, 1.0, 1.0)");
    add_output(d, Value_type::rgba, "vec4(vec3(1.0)-$in($uv).rgb, $in($uv).a)");
    return d;
}

// Quantize / posterize - ported from Material Maker quantize.mmg (MIT).
auto build_quantize() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "quantize";
    d.label    = "Quantize";
    d.category = "Filters";
    add_input(d, "in", Value_type::rgba, "vec4(2.0*vec3(length($uv-vec2(0.5))), 1.0)");
    add_float(d, "steps", "Steps", 4.0f, 2.0f, 32.0f, 1.0f);
    add_output(d, Value_type::rgba, "vec4(floor($in($uv).rgb*$steps)/$steps, $in($uv).a)");
    return d;
}

// Adjust HSV - globals + code ported from Material Maker adjust_hsv.mmg (MIT).
auto build_adjust_hsv() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "adjust_hsv";
    d.label    = "Adjust HSV";
    d.category = "Filters";
    d.global =
        "vec3 rgb_to_hsv(vec3 c) {\n"
        "    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
        "    vec4 p = c.g < c.b ? vec4(c.bg, K.wz) : vec4(c.gb, K.xy);\n"
        "    vec4 q = c.r < p.x ? vec4(p.xyw, c.r) : vec4(c.r, p.yzx);\n"
        "    float d = q.x - min(q.w, q.y);\n"
        "    float e = 1.0e-10;\n"
        "    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);\n"
        "}\n"
        "\n"
        "vec3 hsv_to_rgb(vec3 c) {\n"
        "    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
        "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
        "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
        "}\n";
    add_input(d, "in", Value_type::rgba, "vec4($uv.x, $uv.y, 0.0, 1.0)");
    add_float(d, "hue",        "Hue",        0.0f, -0.5f, 0.5f, 0.01f);
    add_float(d, "saturation", "Saturation", 1.0f,  0.0f, 2.0f, 0.01f);
    add_float(d, "value",      "Value",      1.0f,  0.0f, 2.0f, 0.01f);
    d.code =
        "vec4 $(name_uv)_rgba = $in($uv);\n"
        "vec3 $(name_uv)_hsv = rgb_to_hsv($(name_uv)_rgba.rgb);\n"
        "$(name_uv)_hsv.x += $hue;\n"
        "$(name_uv)_hsv.y = clamp($(name_uv)_hsv.y*$saturation, 0.0, 1.0);\n"
        "$(name_uv)_hsv.z = clamp($(name_uv)_hsv.z*$value, 0.0, 1.0);\n";
    add_output(d, Value_type::rgba, "vec4(hsv_to_rgb($(name_uv)_hsv), $(name_uv)_rgba.a)");
    return d;
}

// Remap / levels - ported from Material Maker remap.mmg (MIT). Rescales a
// grayscale input to [min, max] with optional stepping.
auto build_remap() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "remap";
    d.label    = "Remap";
    d.category = "Filters";
    add_input(d, "in", Value_type::grayscale, "$uv.x");
    add_float(d, "min",  "Min",  0.0f, -10.0f, 10.0f, 0.01f);
    add_float(d, "max",  "Max",  1.0f, -10.0f, 10.0f, 0.01f);
    add_float(d, "step", "Step", 0.0f,   0.0f,  1.0f, 0.01f);
    d.code = "float $(name_uv)_x = $in($uv)*($max-$min);\n";
    add_output(d, Value_type::grayscale, "$min+$(name_uv)_x-mod($(name_uv)_x, max($step, 0.00000001))");
    return d;
}

// ---------------------------------------------------------------------------
// Phase 4b channel ops
// ---------------------------------------------------------------------------

// Combine - ported from Material Maker combine.mmg (MIT). Packs four grayscale
// inputs into one RGBA image.
auto build_combine() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "combine";
    d.label    = "Combine";
    d.category = "Channels";
    add_input(d, "r", Value_type::grayscale, "0.0");
    add_input(d, "g", Value_type::grayscale, "0.0");
    add_input(d, "b", Value_type::grayscale, "0.0");
    add_input(d, "a", Value_type::grayscale, "1.0");
    add_output(d, Value_type::rgba, "vec4($r($uv), $g($uv), $b($uv), $a($uv))");
    return d;
}

// Decompose - ported from Material Maker decompose.mmg (MIT). Splits one RGBA
// input into four grayscale outputs (R, G, B, A). This is the editor's first
// multi-output filter node, exercising erhe::texgen's per-output-index
// composition end to end (each output slot selects a distinct expression).
auto build_decompose() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "decompose";
    d.label    = "Decompose";
    d.category = "Channels";
    add_input(d, "i", Value_type::rgba, "vec4(1.0)");
    add_output(d, Value_type::grayscale, "$i($uv).r");
    add_output(d, Value_type::grayscale, "$i($uv).g");
    add_output(d, Value_type::grayscale, "$i($uv).b");
    add_output(d, Value_type::grayscale, "$i($uv).a");
    return d;
}

// Swap channels - ported from Material Maker swap_channels.mmg (MIT). Each of
// the four output channels selects (via an enum code fragment) a source channel
// or a constant.
auto build_swap_channels() -> Node_descriptor
{
    // The 10 selectable sources are identical for each output channel.
    const std::vector<Enum_value> channel_values{
        Enum_value{"0",  "0.0"},
        Enum_value{"1",  "1.0"},
        Enum_value{"R",  "$in($uv).r"},
        Enum_value{"-R", "1.0-$in($uv).r"},
        Enum_value{"G",  "$in($uv).g"},
        Enum_value{"-G", "1.0-$in($uv).g"},
        Enum_value{"B",  "$in($uv).b"},
        Enum_value{"-B", "1.0-$in($uv).b"},
        Enum_value{"A",  "$in($uv).a"},
        Enum_value{"-A", "1.0-$in($uv).a"}
    };
    Node_descriptor d{};
    d.name     = "swap_channels";
    d.label    = "Swap Channels";
    d.category = "Channels";
    add_input(d, "in", Value_type::rgba, "vec4(1.0)");
    add_enum(d, "out_r", "R", channel_values, 2); // R
    add_enum(d, "out_g", "G", channel_values, 4); // G
    add_enum(d, "out_b", "B", channel_values, 6); // B
    add_enum(d, "out_a", "A", channel_values, 8); // A
    add_output(d, Value_type::rgba, "vec4($out_r, $out_g, $out_b, $out_a)");
    return d;
}

// ---------------------------------------------------------------------------
// Phase 4b utility
// ---------------------------------------------------------------------------

// Gaussian Blur - a buffer-dependent filter (doc/texture-graph-plan.md Phase 5),
// concept ported from Material Maker gaussian_blur_x.mmg (MIT). Material Maker's
// node is a separable X/Y pair wrapped around buffers; this is a self-contained
// single-node 2D Gaussian: the rgba input is a FUNCTION-form input (function =
// true), sampled at a (2R+1)x(2R+1) kernel of texel offsets weighted by a
// Gaussian. Because it samples its input dozens of times, feeding it through a
// Buffer node makes each tap a cheap texture fetch instead of re-inlining the
// whole upstream subtree per tap; without a buffer it still composes and renders
// correctly, just expensively. The loop bound is the (clamped) integer radius,
// so a live radius edit only updates the uniform where the count is unchanged.
auto build_blur() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "blur";
    d.label    = "Gaussian Blur";
    d.category = "Filters";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)", true);
    add_float(d, "radius", "Radius", 0.05f, 0.0f, 0.5f, 0.005f); // blur radius in uv (image fraction)
    // Fixed (2K+1)x(2K+1) tap grid spanning [-radius, radius] in uv, Gaussian
    // weighted (sigma = radius/2). K is a compile-time constant so the loops
    // unroll and a live radius edit only re-uploads the uniform (no recompile).
    d.code =
        "const int $(name_uv)_K = 6;\n"
        "float $(name_uv)_step  = $radius / float($(name_uv)_K);\n"
        "float $(name_uv)_sigma = max(1.0e-4, $radius * 0.5);\n"
        "vec4  $(name_uv)_sum   = vec4(0.0);\n"
        "float $(name_uv)_wsum  = 0.0;\n"
        "for (int $(name_uv)_y = -$(name_uv)_K; $(name_uv)_y <= $(name_uv)_K; ++$(name_uv)_y) {\n"
        "    for (int $(name_uv)_x = -$(name_uv)_K; $(name_uv)_x <= $(name_uv)_K; ++$(name_uv)_x) {\n"
        "        vec2  $(name_uv)_o = vec2(float($(name_uv)_x), float($(name_uv)_y)) * $(name_uv)_step;\n"
        "        float $(name_uv)_c = exp(-0.5*dot($(name_uv)_o, $(name_uv)_o)/($(name_uv)_sigma*$(name_uv)_sigma));\n"
        "        $(name_uv)_sum  += $in($uv + $(name_uv)_o) * $(name_uv)_c;\n"
        "        $(name_uv)_wsum += $(name_uv)_c;\n"
        "    }\n"
        "}\n";
    add_output(d, Value_type::rgba, "$(name_uv)_sum / max($(name_uv)_wsum, 1.0e-4)");
    return d;
}

// Reroute - a pass-through node (one rgba input, one rgba output) for tidying
// graph wiring. Mirrors Material Maker's reroute node concept; the identity
// output lets the composer share the source expression (no extra work).
auto build_reroute() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "reroute";
    d.label    = "Reroute";
    d.category = "Utility";
    add_input(d, "in", Value_type::rgba, "vec4($uv, 0.0, 1.0)");
    add_output(d, Value_type::rgba, "$in($uv)");
    return d;
}

// Registry: name -> descriptor, built once. The vector<const Node_descriptor*>
// keeps palette order and drives the compose self-check. Descriptors live in a
// vector that is fully populated before any pointer into it is taken, so the
// ordered pointer list stays valid (no reallocation after the collect pass).
struct Descriptor_registry
{
    std::vector<Node_descriptor>        descriptors;
    std::vector<const Node_descriptor*> ordered;

    Descriptor_registry()
    {
        descriptors.reserve(32);
        descriptors.push_back(build_uniform());
        descriptors.push_back(build_perlin());
        descriptors.push_back(build_voronoi());
        descriptors.push_back(build_bricks());
        descriptors.push_back(build_shape());
        descriptors.push_back(build_fbm());
        descriptors.push_back(build_noise());
        descriptors.push_back(build_color_noise());
        descriptors.push_back(build_gradient());
        descriptors.push_back(build_circular_gradient());
        descriptors.push_back(build_radial_gradient());
        descriptors.push_back(build_spiral_gradient());
        descriptors.push_back(build_multigradient());
        descriptors.push_back(build_sine_wave());
        descriptors.push_back(build_truchet());
        descriptors.push_back(build_weave());
        descriptors.push_back(build_blend());
        descriptors.push_back(build_colorize());
        descriptors.push_back(build_curve());
        descriptors.push_back(build_transform());
        descriptors.push_back(build_brightness_contrast());
        descriptors.push_back(build_normal_map());
        descriptors.push_back(build_blur());
        descriptors.push_back(build_math());
        descriptors.push_back(build_invert());
        descriptors.push_back(build_quantize());
        descriptors.push_back(build_adjust_hsv());
        descriptors.push_back(build_remap());
        descriptors.push_back(build_combine());
        descriptors.push_back(build_decompose());
        descriptors.push_back(build_swap_channels());
        descriptors.push_back(build_uniform_greyscale());
        descriptors.push_back(build_greyscale());
        descriptors.push_back(build_tones());
        descriptors.push_back(build_tones_map());
        descriptors.push_back(build_tones_range());
        descriptors.push_back(build_tones_step());
        descriptors.push_back(build_tonality());
        descriptors.push_back(build_convert_colorspace());
        descriptors.push_back(build_colormap());
        descriptors.push_back(build_palettize());
        descriptors.push_back(build_default_color());
        descriptors.push_back(build_compare());
        descriptors.push_back(build_ensure_greyscale());
        descriptors.push_back(build_ensure_rgba());
        descriptors.push_back(build_rotate());
        descriptors.push_back(build_scale());
        descriptors.push_back(build_shear());
        descriptors.push_back(build_skew());
        descriptors.push_back(build_mirror());
        descriptors.push_back(build_repeat());
        descriptors.push_back(build_swirl());
        descriptors.push_back(build_spherize());
        descriptors.push_back(build_magnify());
        descriptors.push_back(build_kaleidoscope());
        descriptors.push_back(build_warp());
        descriptors.push_back(build_directional_warp());
        descriptors.push_back(build_refract());
        descriptors.push_back(build_switch("switch",           "Switch (Color)",     Value_type::rgba,      "vec4(vec3(0.0), 1.0)"));
        descriptors.push_back(build_switch("switch_grayscale", "Switch (Grayscale)", Value_type::grayscale, "0.0"));
        descriptors.push_back(build_switch("switch_rgb",       "Switch (RGB)",       Value_type::rgb,       "vec3(0.0)"));
        descriptors.push_back(build_reroute());
        ordered.reserve(descriptors.size());
        for (const Node_descriptor& descriptor : descriptors) {
            ordered.push_back(&descriptor);
        }
    }
};

[[nodiscard]] auto registry() -> const Descriptor_registry&
{
    static const Descriptor_registry s_registry;
    return s_registry;
}

} // anonymous namespace

auto get_texture_node_descriptor(const std::string_view type_name) -> const Node_descriptor*
{
    for (const Node_descriptor* descriptor : registry().ordered) {
        if (descriptor->name == type_name) {
            return descriptor;
        }
    }
    return nullptr;
}

auto all_texture_node_descriptors() -> const std::vector<const Node_descriptor*>&
{
    return registry().ordered;
}

auto check_texture_node_descriptors() -> std::vector<std::string>
{
    std::vector<std::string> failures;
    const erhe::texgen::Composer composer{};
    for (const Node_descriptor* descriptor : registry().ordered) {
        for (std::size_t output_index = 0, end = descriptor->outputs.size(); output_index < end; ++output_index) {
            // A single unconnected node uses each input's default expression,
            // so this exercises every ported GLSL template + parameter
            // substitution without needing an editor graph.
            const erhe::texgen::Compose_node compose_node{*descriptor, 1};
            const erhe::texgen::Shader_code  shader_code = composer.compose(compose_node, output_index);
            const std::string                fragment    = composer.assemble_fragment(shader_code);
            if (fragment.find("(error:") != std::string::npos) {
                failures.push_back(
                    fmt::format("node '{}' output {} ({})", descriptor->name, output_index, erhe::texgen::value_type_name(descriptor->outputs[output_index].type))
                );
            }
        }
    }
    return failures;
}

} // namespace editor
