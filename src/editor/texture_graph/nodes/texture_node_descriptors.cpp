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
//
// The angle range is -720..720 rather than Material Maker's -360..360, so the
// swirl can wind more than one full turn - matching the rotate node's range.
// The range only bounds the UI control; the shader takes any angle.
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
    add_float(d, "angle",  "Angle",    0.0f, -720.0f, 720.0f, 0.2f);
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
// Noise variants
//
// Ported from Material Maker voronoi_triangle.mmg, wavelet_noise.mmg,
// noise_anisotropic.mmg, noise_white.mmg, perlin_color.mmg and shard_fbm.mmg
// (MIT); shard_fbm's core is adapted from https://www.shadertoy.com/view/dlKyWw
// by @ENDESGA.
//
// Symbols Material Maker gives generic names are prefixed here, because erhe
// deduplicates globals by exact string match: a bare "hash", "vmax",
// "cellPoint" or "s3" would sooner or later be defined by a second descriptor
// and emitted twice, failing to link only when both nodes share a graph (see
// the Transform family note). shard_fbm's hash -> shard_hash, and
// voronoi_triangle's helpers -> tri_*.
//
// Not ported:
// - voronoi2: its global, inline code and parameters are byte identical to
//   voronoi.mmg, which the existing voronoi node already is. Its only
//   difference is a "fill" output needing the Fill family's iterate-buffer
//   machinery, so porting it would add a duplicate node and a
//   double-definition hazard for no capability.
// - clouds_noise, directional_noise, crystal: compound nodes with no
//   shader_model of their own.
// - fbm2 / fbm3 / fbm4: 13-17 KB of near-duplicate basis libraries. What is
//   worth having is their extra bases (simplex, cellular3..8, voronoise,
//   gabor), which belong as added values on the existing fbm node's enum
//   rather than as three more nodes.
// ---------------------------------------------------------------------------

// Voronoi (triangle) - ported from Material Maker voronoi_triangle.mmg (MIT).
// Triangular-cell Voronoi, with distance, border, cell-color, UV and normal
// outputs.
auto build_voronoi_triangle() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "voronoi_triangle";
    d.label    = "Voronoi Triangle";
    d.category = "Generators";
    d.global =
        "// Based on https://www.shadertoy.com/view/ss3fW4\n"
        "const float tri_s3 = 0.866025;\n"
        "\n"
        "vec3 tri_sd_edges(vec2 p) {\n"
        "    return vec3(\n"
        "        dot(p, vec2(0,-1)),\n"
        "        dot(p, vec2(tri_s3, 0.5)),\n"
        "        dot(p, vec2(-tri_s3, 0.5))\n"
        "    );\n"
        "}\n"
        "\n"
        "float tri_sd(vec2 p) {\n"
        "    vec3 t = tri_sd_edges(p);\n"
        "    return max(t.x, max(t.y, t.z));\n"
        "}\n"
        "\n"
        "vec3 tri_primary_axis(vec3 p) {\n"
        "    vec3 a = abs(p);\n"
        "    return (1.0-step(a.xyz, a.yzx))*step(a.zxy, a.xyz)*sign(p);\n"
        "}\n"
        "\n"
        "vec3 tri_sdg_border(vec2 pt1, vec2 pt2) {\n"
        "    vec3 tbRel = tri_sd_edges(pt2 - pt1);\n"
        "    vec3 axis = tri_primary_axis(tbRel);\n"
        "\n"
        "    vec2 gA = vec2(0,-1);\n"
        "    vec2 gB = vec2(tri_s3, 0.5);\n"
        "    vec2 gC = vec2(-tri_s3, 0.5);\n"
        "\n"
        "    vec2 norA = gC * axis.x + gA * axis.y + gB * axis.z;\n"
        "    vec2 norB = gB * -axis.x + gC * -axis.y + gA * -axis.z;\n"
        "\n"
        "    vec2 dir = gA * axis.x + gB * axis.y + gC * axis.z;\n"
        "    vec2 corner = dir * dot(dir, pt1 - pt2) * 2.0/3.0;\n"
        "\n"
        "    mat2 r90 = mat2(vec2(0.0,-1.0),vec2(1.0,0.0));\n"
        "\n"
        "    bool isEdge = axis.x + axis.y + axis.z < 0.0;\n"
        "\n"
        "    if (isEdge) {\n"
        "        corner = pt2 + corner;\n"
        "        vec2 ca = corner + min(0.0, dot(corner, -norA)) * norA;\n"
        "        vec2 cb = corner + max(0.0, dot(corner, -norB)) * norB;\n"
        "        float side = step(dot(corner, dir * r90), 0.0);\n"
        "        corner = mix(cb, ca, side);\n"
        "    } else {\n"
        "        corner = pt1 - corner;\n"
        "        vec2 ca = corner + max(0.0, dot(corner, -norA)) * norA;\n"
        "        vec2 cb = corner + min(0.0, dot(corner, -norB)) * norB;\n"
        "        float side = step(dot(corner, dir * r90), 0.0);\n"
        "        corner = mix(ca, cb, side);\n"
        "    }\n"
        "\n"
        "    vec2 nor = normalize(corner);\n"
        "    float d = length(corner);\n"
        "    return vec3(abs(d), nor);\n"
        "}\n"
        "\n"
        "float tri_vmax(vec3 v) { return max(v.x, max(v.y, v.z)); }\n"
        "\n"
        "vec4 tri_cell_point(vec2 n, vec2 f, vec2 cell, float r, float seed, vec2 s, vec2 st) {\n"
        "    vec2 coord = n + cell;\n"
        "    vec2 o = r*rand2(seed + mod(n + cell + s, s));\n"
        "    vec2 point = cell + o - f;\n"
        "    point *= st;\n"
        "    return vec4(point, coord);\n"
        "}\n"
        "\n"
        "vec2 tri_coord(vec2 x, vec2 s, vec2 st, float r, float seed) {\n"
        "    vec2 n = floor(x);\n"
        "    vec2 f = fract(x);\n"
        "\n"
        "    vec2 closestCoord;\n"
        "\n"
        "    const int reach = 2;\n"
        "    float closestDist = 8.0;\n"
        "    for( int j = -reach; j <= reach; j++ )\n"
        "    for( int i = -reach; i <= reach; i++ )\n"
        "    {\n"
        "        vec2 cell = vec2(float(i), float(j));\n"
        "        vec4 point = tri_cell_point(n,f,cell,r,seed,s,st);\n"
        "\n"
        "        float dist = tri_vmax(tri_sd_edges(point.xy));\n"
        "\n"
        "        if( tri_vmax(tri_sd_edges(point.xy)) < closestDist )\n"
        "        {\n"
        "            closestDist = dist;\n"
        "            closestCoord = point.zw;\n"
        "        }\n"
        "    }\n"
        "    return closestCoord;\n"
        "}\n"
        "\n"
        "mat3 tri_voronoi( vec2 x, vec2 s, vec2 st, float r, float seed) {\n"
        "    x *= s;\n"
        "    vec2 n = floor(x);\n"
        "    vec2 f = fract(x);\n"
        "\n"
        "    vec2 closestCell, closestPoint, nor;\n"
        "\n"
        "    const int reach = 3;\n"
        "    float closestDist = 8.0;\n"
        "\n"
        "    for( int j = -reach; j <= reach; j++ )\n"
        "    for( int i = -reach; i <= reach; i++ )\n"
        "    {\n"
        "        vec2 cell = vec2(float(i), float(j));\n"
        "        vec2 point = tri_cell_point(n,f,cell,r,seed,s,st).xy;\n"
        "\n"
        "        float dist = tri_sd(point);\n"
        "\n"
        "        if( dist < closestDist )\n"
        "        {\n"
        "            closestDist = dist;\n"
        "            closestPoint = point;\n"
        "            closestCell = cell;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    closestDist = 8.0;\n"
        "    for( int j = -reach-1; j <= reach+1; j++ )\n"
        "    for( int i = -reach-1; i <= reach+1; i++ )\n"
        "    {\n"
        "        vec2 cell = closestCell + vec2(float(i), float(j));\n"
        "        vec2 coord = n + cell;\n"
        "            vec2 o = r*rand2(seed + mod(n + cell + s, s));\n"
        "            vec2 point = cell + o - f;\n"
        "        point *= st;\n"
        "\n"
        "        float dist = tri_sd(closestPoint - point);\n"
        "\n"
        "        if( dist > 0.00001 ) {\n"
        "            vec3 sdg = tri_sdg_border(closestPoint, point);\n"
        "            if (sdg.x < closestDist) {\n"
        "                closestDist = sdg.x;\n"
        "                nor = sdg.zy;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "\n"
        "    return mat3(\n"
        "        vec3(closestDist,closestPoint),\n"
        "        vec3(nor,0),\n"
        "        vec3(tri_coord(x,s,st,r,seed),0));\n"
        "}\n";
    add_float(d, "scale_x",    "Scale X",    4.0f,  1.0f, 32.0f, 1.0f);
    add_float(d, "scale_y",    "Scale Y",    4.0f,  1.0f, 32.0f, 1.0f);
    add_float(d, "stretch_x",  "Stretch X",  1.0f,  0.0f,  1.0f, 0.01f);
    add_float(d, "stretch_y",  "Stretch Y",  1.0f,  0.0f,  1.0f, 0.01f);
    add_float(d, "randomness", "Randomness", 0.85f, 0.0f,  1.0f, 0.01f);
    d.code =
        "mat3 $(name_uv)_m3 = tri_voronoi($uv, vec2($scale_x, $scale_y), vec2($stretch_y,$stretch_x), $randomness, $seed);\n";
    add_output(d, Value_type::grayscale, "length($(name_uv)_m3[0].yz)");
    add_output(d, Value_type::grayscale, "$(name_uv)_m3[0].x");
    add_output(d, Value_type::rgb,       "rand3(floor(fract($(name_uv)_m3[2].xy/vec2($scale_x,$scale_y))*vec2($scale_x,$scale_y)))");
    add_output(d, Value_type::rgb,       "vec3(-$(name_uv)_m3[0].yz*0.5+0.5,rand(fract($(name_uv)_m3[2].xy/vec2($scale_x,$scale_y))))");
    add_output(d, Value_type::rgb,       "vec3(0.5)+0.5*normalize(vec3(vec2(-$(name_uv)_m3[1].y,-$(name_uv)_m3[1].x), -1.0))");
    return d;
}

// Wavelet noise - ported from Material Maker wavelet_noise.mmg (MIT). The
// "type" enum substitutes an int literal choosing how octaves combine
// (positive additive, negative multiplicative).
auto build_wavelet_noise() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "wavelet_noise";
    d.label    = "Wavelet Noise";
    d.category = "Generators";
    d.global =
        "float wavelet(vec2 uv, vec2 size, float s, float frequency, float offset) {\n"
        "    uv = mod(uv, size);\n"
        "    vec2 seed = fract(floor(uv)*0.1236754+vec2(s));\n"
        "    uv = fract(uv);\n"
        "    vec2 ruv = uv-0.5;\n"
        "    float a = rand(seed)*6.28;\n"
        "    float ca = cos(a);\n"
        "    float sa = sin(a);\n"
        "    ruv = vec2(ca*ruv.x + sa*ruv.y, -sa*ruv.x + ca*ruv.y);\n"
        "    return (0.5*sin(ruv.x*6.28*frequency+offset)+0.5)*max(0.0, 1.0-2.0*length(uv-vec2(0.5)));\n"
        "}\n"
        "\n"
        "float wavelet_noise(vec2 uv, vec2 size, int iterations, float persistence, float seed, float frequency, float offset, float type) {\n"
        "    float rv = 0.0;\n"
        "    float acc = 0.0;\n"
        "    vec2 seed2 = rand2(vec2(seed));\n"
        "    vec2 local_uv = uv * size;\n"
        "    float q = 1.0;\n"
        "    for (int i = 0; i < iterations; ++i) {\n"
        "        rv += q*wavelet(local_uv, size, seed, frequency, offset);\n"
        "        rv += q*wavelet(local_uv+vec2(0.5), size, seed+0.1, frequency, offset);\n"
        "        acc += q;\n"
        "        if (type > 0.0) {\n"
        "            local_uv += type*uv;\n"
        "            size += vec2(type);\n"
        "        } else {\n"
        "            local_uv *= -type;\n"
        "            size *= -type;\n"
        "        }\n"
        "        local_uv += seed2;\n"
        "        seed2 = rand2(seed2);\n"
        "        q *= persistence;\n"
        "        seed += 0.1;\n"
        "    }\n"
        "    return rv / acc;\n"
        "}\n";
    add_enum(
        d, "type", "Type",
        {
            Enum_value{.label = "Add 1",  .code = "1"},
            Enum_value{.label = "Add 2",  .code = "2"},
            Enum_value{.label = "Add 3",  .code = "3"},
            Enum_value{.label = "Mult 2", .code = "-2"},
            Enum_value{.label = "Mult 3", .code = "-3"}
        },
        4
    );
    add_float(d, "scale_x",     "Scale X",     4.0f,  1.0f, 32.0f, 1.0f);
    add_float(d, "scale_y",     "Scale Y",     4.0f,  1.0f, 32.0f, 1.0f);
    add_float(d, "iterations",  "Octaves",     3.0f,  1.0f, 10.0f, 1.0f);
    add_float(d, "persistence", "Persistence", 0.5f,  0.0f,  1.0f, 0.01f);
    add_float(d, "frequency",   "Frequency",   1.0f,  0.0f,  2.0f, 0.01f);
    add_float(d, "offset",      "Offset",      0.0f, -1.0f,  1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "wavelet_noise($uv, vec2($scale_x, $scale_y), int($iterations), $persistence, $seed, $frequency, $offset, $type)"
    );
    return d;
}

// Anisotropic noise - ported from Material Maker noise_anisotropic.mmg (MIT).
// Rows of value noise stretched along x, for brushed-metal style looks.
auto build_noise_anisotropic() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "noise_anisotropic";
    d.label    = "Anisotropic Noise";
    d.category = "Generators";
    d.global =
        "float anisotropic(vec2 uv, vec2 size, float seed, float smoothness, float interpolation) {\n"
        "    vec2 seed2 = rand2(vec2(seed, 1.0-seed));\n"
        "\n"
        "    vec2 xy = floor(uv*size);\n"
        "    vec2 offset = vec2(rand(seed2 + xy.y), 0.0);\n"
        "    vec2 xy_offset = floor(uv * size + offset );\n"
        "    float f0 = rand(seed2+mod(xy_offset, size));\n"
        "    float f1 = rand(seed2+mod(xy_offset+vec2(1.0, 0.0), size));\n"
        "    float mixer = clamp( (fract(uv.x*size.x+offset.x) -.5) / smoothness + 0.5, 0.0, 1.0 );\n"
        "    float smooth_mix = smoothstep(0.0, 1.0, mixer);\n"
        "    float linear = mix(f0, f1, mixer);\n"
        "    float smoothed = mix(f0, f1, smooth_mix);\n"
        "\n"
        "    return mix(linear, smoothed, interpolation);\n"
        "}\n";
    add_float(d, "scale_x",       "Scale X",         4.0f, 1.0f,   32.0f, 1.0f);
    add_float(d, "scale_y",       "Scale Y",       256.0f, 1.0f, 1024.0f, 1.0f);
    add_float(d, "smoothness",    "Smoothness",      1.0f, 0.0f,    1.0f, 0.01f);
    add_float(d, "interpolation", "Interpolation",   1.0f, 0.0f,    1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "anisotropic($uv, vec2($scale_x, $scale_y), $seed, $smoothness, $interpolation)"
    );
    return d;
}

// White noise - ported from Material Maker noise_white.mmg (MIT). Material
// Maker wraps the call in a per-node instance helper; with one call site the
// wrapper carries no benefit, so the call is written directly.
auto build_noise_white() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "noise_white";
    d.label    = "White Noise";
    d.category = "Generators";
    d.global =
        "float white_noise(vec2 uv, float size, float seed) {\n"
        "    vec2 seed2 = rand2(vec2(seed, 1.0-seed));\n"
        "    uv /= size;\n"
        "    vec2 point_pos = floor(uv)+vec2(0.5);\n"
        "    float color = rand(seed2+point_pos);\n"
        "    return color;\n"
        "}\n";
    add_size(d, "size", "Grid Size", 2, 12, 11);
    add_output(d, Value_type::grayscale, "white_noise($uv, 1.0/$size, $seed)");
    return d;
}

// Perlin color - ported from Material Maker perlin_color.mmg (MIT). The rgb
// sibling of the existing perlin node (it interpolates rand3 instead of rand),
// filling the gap noted against `perlin` in the node comparison table.
auto build_perlin_color() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "perlin_color";
    d.label    = "Perlin Color";
    d.category = "Generators";
    d.global =
        "vec3 perlin_color(vec2 uv, vec2 size, int iterations, float persistence, float seed) {\n"
        "    vec2 seed2 = rand2(vec2(seed, 1.0-seed));\n"
        "    vec3 rv = vec3(0.0);\n"
        "    float coef = 1.0;\n"
        "    float acc = 0.0;\n"
        "    for (int i = 0; i < iterations; ++i) {\n"
        "        vec2 step = vec2(1.0)/size;\n"
        "        vec2 xy = floor(uv*size);\n"
        "        vec3 f0 = rand3(seed2+mod(xy, size));\n"
        "        vec3 f1 = rand3(seed2+mod(xy+vec2(1.0, 0.0), size));\n"
        "        vec3 f2 = rand3(seed2+mod(xy+vec2(0.0, 1.0), size));\n"
        "        vec3 f3 = rand3(seed2+mod(xy+vec2(1.0, 1.0), size));\n"
        "        vec2 mixval = smoothstep(0.0, 1.0, fract(uv*size));\n"
        "        rv += coef * mix(mix(f0, f1, mixval.x), mix(f2, f3, mixval.x), mixval.y);\n"
        "        acc += coef;\n"
        "        size *= 2.0;\n"
        "        coef *= persistence;\n"
        "    }\n"
        "\n"
        "    return rv / acc;\n"
        "}\n";
    add_float(d, "scale_x",     "Scale X",     4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "scale_y",     "Scale Y",     4.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "iterations",  "Octaves",     3.0f, 1.0f, 10.0f, 1.0f);
    add_float(d, "persistence", "Persistence", 0.5f, 0.0f,  1.0f, 0.05f);
    add_output(
        d, Value_type::rgb,
        "perlin_color($uv, vec2($scale_x, $scale_y), int($iterations), $persistence, $seed)"
    );
    return d;
}

// Shard FBM - ported from Material Maker shard_fbm.mmg (MIT), whose core is
// adapted from https://www.shadertoy.com/view/dlKyWw by @ENDESGA. Both map
// inputs default to their matching parameter, as in Material Maker.
//
// One fix against the .mmg: its output calls pow(2, ...) with an int literal.
// Godot's compiler accepts that; glslang is stricter about pow's genType
// arguments, so it is written pow(2.0, ...).
auto build_shard_fbm() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "shard_fbm";
    d.label    = "Shard FBM";
    d.category = "Generators";
    d.global =
        "// Adapted from https://www.shadertoy.com/view/dlKyWw by @ENDESGA\n"
        "\n"
        "vec3 shard_hash(vec3 p)\n"
        "{\n"
        "    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5,183.3,246.1)), dot(p, vec3(113.5, 271.9, 124.6)));\n"
        "    p = fract(sin(p) * 43758.5453123);\n"
        "    return p;\n"
        "}\n"
        "\n"
        "float shard_noise(vec3 p, vec3 size, float sharpness, float seed) {\n"
        "    vec3 ip = floor(p);\n"
        "    vec3 fp = fract(p);\n"
        "\n"
        "    float v = 0.0, t = 0.0;\n"
        "\n"
        "    for (int z = -1; z <= 1; z++) {\n"
        "        for (int y = -2; y <= 2; y++) {\n"
        "            for (int x = -2; x <= 2; x++) {\n"
        "\n"
        "                vec3 o = vec3(float(x), float(y), float(z));\n"
        "                vec3 io = mod(ip + o, size);\n"
        "                vec3 h = shard_hash(io + seed);\n"
        "                vec3 r = fp - (o + h);\n"
        "\n"
        "                float w = exp2(-6.283185*dot(r, r));\n"
        "                // tanh deconstruction and optimization by @Xor\n"
        "                float s = sharpness * dot(r, shard_hash(io + vec3(11, 31, 47)) - 0.5);\n"
        "                v += w * s*inversesqrt(1.0+s*s);\n"
        "                t += w;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    return ((v / t) * 0.5) + 0.5;\n"
        "}\n"
        "\n"
        "float fbm_shard(vec3 coord, vec3 size, int folds, int octaves, float persistence, float sharpness, float seed) {\n"
        "    float normalize_factor = 0.0;\n"
        "    float value = 0.0;\n"
        "    float scale = 1.0;\n"
        "    for (int i = 0; i < octaves; i++) {\n"
        "        float noise = shard_noise(coord*size, size, sharpness, seed);\n"
        "        for (int f = 0; f < folds; ++f) {\n"
        "            noise = abs(2.0*noise-1.0);\n"
        "        }\n"
        "        value += noise * scale;\n"
        "        normalize_factor += scale;\n"
        "        size *= 2.0;\n"
        "        scale *= persistence;\n"
        "    }\n"
        "    return value / normalize_factor;\n"
        "}\n";
    add_input(d, "sharp_in",  Value_type::grayscale, "$sharp");
    add_input(d, "offset_in", Value_type::grayscale, "$off");
    add_float(d, "sharp",  "Sharpness",   0.7f, 0.0f,  1.0f, 0.01f);
    add_float(d, "sx",     "Scale X",     7.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "sy",     "Scale Y",     7.0f, 1.0f, 32.0f, 1.0f);
    add_float(d, "folds",  "Folds",       0.0f, 0.0f,  5.0f, 1.0f);
    add_float(d, "iter",   "Octaves",     4.0f, 1.0f,  8.0f, 1.0f);
    add_float(d, "per",    "Persistence", 0.5f, 0.0f,  1.0f, 0.01f);
    add_float(d, "off",    "Offset",      0.0f, 0.0f,  1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "fbm_shard(vec3($uv, $offset_in($uv)), vec3($sx, $sy, 1.0), int($folds), int($iter), $per, "
        "pow(2.0, $sharp_in($uv)*$sharp*8.0), $seed)"
    );
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
// Deterministic patterns
//
// Ported from Material Maker pattern.mmg, beehive.mmg, cairo.mmg,
// arc_pavement.mmg, iching.mmg, runes.mmg, roman_numerals.mmg,
// seven_segment.mmg, scratches.mmg, profile.mmg and japanese_glyphs.mmg (MIT).
// Each is a self-contained procedural generator: same uv in, same value out,
// no buffers and no iteration state.
//
// Every symbol is prefixed, because erhe deduplicates globals by exact string
// match: Material Maker's bare "s", "l", "m", "fs", "box", "grid", "line",
// "pavement" and "wave_sine" would sooner or later be defined by a second
// descriptor and emitted twice, failing to link only when both nodes share a
// graph. Prefixes here: pat_, ap_, iching_, rune_, rn_, ss_, jg_.
//
// Material Maker resolves an "includes" list against shared snippet nodes;
// erhe has no include mechanism, so the pulled-in helpers are inlined into the
// descriptor that needs them, under that descriptor's prefix (sdline2's sdLine
// -> rune_sd_line / rn_sd_line / jg_sdLine, curve's sdBezier -> jg_sdBezier,
// sdbox's sd_box -> jg_sd_box). Three copies of a four-line segment distance
// is the cost of keeping each global self-contained.
//
// Not ported:
// - splines, polycurve: both are driven by a Material Maker point-list
//   parameter widget ("splines" / "polyline") whose GLSL - the per-instance
//   $(name)_splines / bezier_uv_$name function - is generated from the edited
//   points. erhe has no such Parameter_kind, and adding one is a widget +
//   parameter-codegen feature, not a GLSL port. Their distance-to-bezier
//   globals are worth revisiting if that widget is ever added.
// - dirt: a compound graph node (26 sub-nodes, "type": "graph") with no
//   shader_model of its own.
//
// Two fixes against the .mmg, both places where Godot's GLSL is laxer than
// glslang: roman_numerals calls clamp(x, 0, 1) with int literals (-> 0.0, 1.0)
// and initializes const int val[] with a C-style brace list (-> int[6](...)).
// ---------------------------------------------------------------------------

// Pattern - ported from Material Maker pattern.mmg (MIT). Combines a
// horizontal and a vertical waveform with a selectable operator; both the
// waveform and the combiner enums substitute a function name suffix.
auto build_pattern() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "pattern";
    d.label    = "Pattern";
    d.category = "Patterns";
    d.global =
        "float pat_wave_constant(float x) {\n"
        "    return 1.0;\n"
        "}\n"
        "\n"
        "float pat_wave_sine(float x) {\n"
        "    return 0.5-0.5*cos(3.14159265359*2.0*x);\n"
        "}\n"
        "\n"
        "float pat_wave_triangle(float x) {\n"
        "    x = fract(x);\n"
        "    return min(2.0*x, 2.0-2.0*x);\n"
        "}\n"
        "\n"
        "float pat_wave_sawtooth(float x) {\n"
        "    return fract(x);\n"
        "}\n"
        "\n"
        "float pat_wave_square(float x) {\n"
        "    return (fract(x) < 0.5) ? 0.0 : 1.0;\n"
        "}\n"
        "\n"
        "float pat_wave_bounce(float x) {\n"
        "    x = 2.0*(fract(x)-0.5);\n"
        "    return sqrt(1.0-x*x);\n"
        "}\n"
        "\n"
        "float pat_mix_mul(float x, float y) {\n"
        "    return x*y;\n"
        "}\n"
        "\n"
        "float pat_mix_add(float x, float y) {\n"
        "    return min(x+y, 1.0);\n"
        "}\n"
        "\n"
        "float pat_mix_max(float x, float y) {\n"
        "    return max(x, y);\n"
        "}\n"
        "\n"
        "float pat_mix_min(float x, float y) {\n"
        "    return min(x, y);\n"
        "}\n"
        "\n"
        "float pat_mix_xor(float x, float y) {\n"
        "    return min(x+y, 2.0-x-y);\n"
        "}\n"
        "\n"
        "float pat_mix_pow(float x, float y) {\n"
        "    return pow(x, y);\n"
        "}\n";
    const std::vector<Enum_value> wave_values{
        Enum_value{"Sine",     "sine"},
        Enum_value{"Triangle", "triangle"},
        Enum_value{"Square",   "square"},
        Enum_value{"Sawtooth", "sawtooth"},
        Enum_value{"Constant", "constant"},
        Enum_value{"Bounce",   "bounce"}
    };
    add_enum(
        d, "mix", "Combine",
        {
            Enum_value{"Multiply", "mul"},
            Enum_value{"Add",      "add"},
            Enum_value{"Max",      "max"},
            Enum_value{"Min",      "min"},
            Enum_value{"Xor",      "xor"},
            Enum_value{"Pow",      "pow"}
        },
        0
    );
    add_enum (d, "x_wave",  "X Pattern", wave_values, 0);
    add_float(d, "x_scale", "X Repeat",  4.0f, 0.0f, 32.0f, 1.0f);
    add_enum (d, "y_wave",  "Y Pattern", wave_values, 0);
    add_float(d, "y_scale", "Y Repeat",  4.0f, 0.0f, 32.0f, 1.0f);
    add_output(
        d, Value_type::grayscale,
        "pat_mix_$(mix)(pat_wave_$(x_wave)($x_scale*$uv.x), pat_wave_$(y_wave)($y_scale*$uv.y))"
    );
    return d;
}

// Beehive - ported from Material Maker beehive.mmg (MIT). Hexagonal tiling
// with a pattern, a per-cell random color and a per-cell UV map output.
auto build_beehive() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "beehive";
    d.label    = "Beehive";
    d.category = "Patterns";
    d.global =
        "float beehive_dist(vec2 p) {\n"
        "    vec2 s = vec2(1.0, 1.73205080757);\n"
        "    p = abs(p);\n"
        "    return max(dot(p, s*0.5), p.x);\n"
        "}\n"
        "\n"
        "vec4 beehive_center(vec2 p) {\n"
        "    vec2 s = vec2(1.0, 1.73205080757);\n"
        "    vec4 hC = floor(vec4(p, p - vec2(0.5, 1.0))/vec4(s, s)) + 0.5;\n"
        "    vec4 h = vec4(p - hC.xy*s, p - (hC.zw + 0.5)*s);\n"
        "    return dot(h.xy, h.xy) < dot(h.zw, h.zw) ? vec4(h.xy, hC.xy) : vec4(h.zw, hC.zw + 9.73);\n"
        "}\n";
    add_float(d, "sx", "Size X", 4.0f, 1.0f, 64.0f, 1.0f);
    add_float(d, "sy", "Size Y", 4.0f, 1.0f, 64.0f, 1.0f);
    d.code =
        "vec2 $(name_uv)_bh_uv = $uv*vec2($sx, $sy*1.73205080757);\n"
        "vec4 $(name_uv)_bh_center = beehive_center($(name_uv)_bh_uv);\n";
    add_output(d, Value_type::grayscale, "1.0-2.0*beehive_dist($(name_uv)_bh_center.xy)");
    add_output(d, Value_type::rgb,       "rand3(fract($(name_uv)_bh_center.zw/vec2($sx, $sy))+vec2(float($seed)))");
    add_output(
        d, Value_type::rgb,
        "vec3(vec2(0.5)+$(name_uv)_bh_center.xy, rand(fract($(name_uv)_bh_center.zw/vec2($sx, $sy))+vec2(float($seed))))"
    );
    return d;
}

// Cairo - ported from Material Maker cairo.mmg (MIT). Cairo pentagonal tiling.
// Material Maker's second output is a "fill" bounding box for its Fill
// companion node, which needs the Fill family's iterate-buffer machinery; only
// the pattern output is ported, so cairo_bbox is not carried over either.
auto build_cairo() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "cairo";
    d.label    = "Cairo";
    d.category = "Patterns";
    d.global =
        "float cairo_round(vec2 uv, float angle, float k) {\n"
        "    vec2 cell = floor(uv);\n"
        "    float ca = cos(angle);\n"
        "    float sa = sin(angle);\n"
        "    vec2 corner = fract(uv)-0.5;\n"
        "    uv = 0.5-abs(corner);\n"
        "    uv = mix(uv, uv.yx, mod(cell.x+cell.y, 2.0));\n"
        "    float side = dot(vec2(-sa, ca), uv);\n"
        "    float d1 = abs(side);\n"
        "    float d2 = abs(dot(vec2(-sa, ca), mix(vec2(uv.x, 1.0-uv.y), vec2(1.0-uv.x, uv.y), step(side, 0.0))));\n"
        "    float d3 = abs(dot(vec2(ca, sa), uv));\n"
        "    float d4 = mix(0.5-uv.x, 0.5-uv.y, step(side, 0.0));\n"
        "    return clamp(-log2(exp2(-k*d1)+exp2(-k*d2)+exp2(-k*d3)+exp2(-k*d4))/k, 0.0, 1.0);\n"
        "}\n";
    add_float(d, "sx",    "Size X", 4.0f,  1.0f, 64.0f, 1.0f);
    add_float(d, "sy",    "Size Y", 4.0f,  1.0f, 64.0f, 1.0f);
    add_float(d, "angle", "Angle", 30.0f,  0.0f, 90.0f, 0.01f);
    add_float(d, "round", "Round",  0.0f,  0.0f,  1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "cairo_round($uv*vec2($sx, $sy), $angle*0.01745329251, 200.0-190.0*$round)"
    );
    return d;
}

// Arc pavement - ported from Material Maker arc_pavement.mmg (MIT). Fan-shaped
// cobblestone arcs, with a bricks pattern, a per-brick random color and a
// per-brick UV map output. Material Maker's PI constant comes from its shader
// preamble; erhe has none, so it is spelled out.
auto build_arc_pavement() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "arc_pavement";
    d.label    = "Arc Pavement";
    d.category = "Patterns";
    d.global =
        "const float ap_pi = 3.14159265359;\n"
        "\n"
        "float ap_pavement(vec2 uv, float bevel, float mortar) {\n"
        "    uv = abs(uv-vec2(0.5));\n"
        "    return clamp((0.5*(1.0-mortar)-max(uv.x, uv.y))/max(0.0001, bevel), 0.0, 1.0);\n"
        "}\n"
        "\n"
        "vec4 arc_pavement(vec2 uv, float acount, float lcount, out vec2 seed) {\n"
        "    float radius = (0.5/sqrt(2.0));\n"
        "    float uvx = uv.x;\n"
        "    uv.x = 0.5*fract(uv.x+0.5)+0.25;\n"
        "    float center = (uv.x-0.5)/radius;\n"
        "    center *= center;\n"
        "    center = floor(acount*(uv.y-radius*sqrt(1.0-center))+0.5)/acount;\n"
        "    vec2 v = uv-vec2(0.5, center);\n"
        "    float cornerangle = 0.85/acount+0.25*ap_pi;\n"
        "    float acountangle = (ap_pi-2.0*cornerangle)/(lcount+floor(mod(center*acount, 2.0)));\n"
        "    float angle = mod(atan(v.y, v.x), 2.0*ap_pi);\n"
        "    float base_angle;\n"
        "    float local_uvy = 0.5+acount*(length(v)-radius)*(1.66-0.71*cos(1.44*(angle-ap_pi*0.5)));\n"
        "    vec2 local_uv;\n"
        "    if (angle < cornerangle) {\n"
        "        base_angle = 0.25*ap_pi;\n"
        "        local_uv = vec2((angle-0.25*ap_pi)/cornerangle*0.4*acount+0.55, 1.0-local_uvy);\n"
        "        seed = vec2(fract(center), 0.0);\n"
        "    } else if (angle > ap_pi-cornerangle) {\n"
        "        base_angle = 0.75*ap_pi;\n"
        "        local_uv = vec2(local_uvy, 0.45-(0.75*ap_pi-angle)/cornerangle*0.4*acount);\n"
        "        seed = vec2(fract(center), 0.0);\n"
        "    } else {\n"
        "        base_angle = cornerangle+(floor((angle-cornerangle)/acountangle)+0.5)*acountangle;\n"
        "        local_uv = vec2((angle-base_angle)/acountangle+0.5, 1.0-local_uvy);\n"
        "        seed = vec2(fract(center), base_angle);\n"
        "    }\n"
        "    vec2 brick_center = vec2(0.5, center)+radius*vec2(cos(base_angle), sin(base_angle));\n"
        "    return vec4(brick_center.x+uvx-uv.x, brick_center.y, local_uv);\n"
        "}\n";
    add_float(d, "repeat", "Repeat", 2.0f, 1.0f,  4.0f, 1.0f);
    add_float(d, "rows",   "Rows",   8.0f, 4.0f, 16.0f, 1.0f);
    add_float(d, "bricks", "Bricks", 8.0f, 4.0f, 16.0f, 1.0f);
    add_float(d, "mortar", "Mortar", 0.1f, 0.0f,  0.5f, 0.01f);
    add_float(d, "bevel",  "Bevel",  0.1f, 0.0f,  0.5f, 0.01f);
    d.code =
        "vec2 $(name_uv)_ap_uv = fract($uv)*vec2($repeat, -1.0);\n"
        "vec2 $(name_uv)_ap_seed;\n"
        "vec4 $(name_uv)_ap = arc_pavement($(name_uv)_ap_uv, $rows, $bricks, $(name_uv)_ap_seed);\n";
    add_output(d, Value_type::grayscale, "ap_pavement($(name_uv)_ap.zw, $bevel, 2.0*$mortar)");
    add_output(d, Value_type::rgb,       "rand3($(name_uv)_ap_seed)");
    add_output(d, Value_type::rgb,       "vec3($(name_uv)_ap.zw, 0.0)");
    return d;
}

// I Ching - ported from Material Maker iching.mmg (MIT). A grid of random
// I Ching hexagrams. Material Maker's shader_model default for the grid size
// is 0, below its own minimum of 2; the .mmg instance value (4) is used here.
auto build_iching() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "iching";
    d.label    = "I Ching";
    d.category = "Patterns";
    d.global =
        "float iching_hexagram(vec2 uv, float seed) {\n"
        "    int value = int(32.0*rand(floor(uv)+vec2(seed)));\n"
        "    float base = step(0.5, fract(fract(uv.y)*6.5))*step(0.04, fract(uv.y+0.02))*step(0.2, fract(uv.x+0.1));\n"
        "    int bit = int(fract(uv.y)*6.5);\n"
        "    return base*step(0.1*step(float(bit & value), 0.5), fract(uv.x+0.55));\n"
        "}\n";
    add_float(d, "columns", "Size X", 4.0f, 2.0f, 32.0f, 1.0f);
    add_float(d, "rows",    "Size Y", 4.0f, 2.0f, 32.0f, 1.0f);
    add_output(d, Value_type::grayscale, "iching_hexagram(vec2($columns, $rows)*$uv, float($seed))");
    return d;
}

// Runes - ported from Material Maker runes.mmg (MIT). A grid of random
// four-stroke runes, each stroke snapped to a 2x3 grid and touching one edge
// of its cell. sdLine comes from Material Maker's sdline2 include.
auto build_runes() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "runes";
    d.label    = "Runes";
    d.category = "Patterns";
    d.global =
        "vec2 rune_sd_line(vec2 p, vec2 a, vec2 b) {\n"
        "    vec2 pa = p-a, ba = b-a;\n"
        "    float h = clamp(dot(pa, ba)/dot(ba, ba), 0.0, 1.0);\n"
        "    return vec2(length(pa-ba*h), h);\n"
        "}\n"
        "\n"
        "float rune_thick_line(vec2 uv, vec2 posA, vec2 posB) {\n"
        "    return clamp(1.1-20.0*rune_sd_line(uv, posA, posB).x, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "// Makes a rune in the 0..1 uv space; s selects which rune to draw.\n"
        "float rune_glyph(vec2 uv, float s) {\n"
        "    float finalLine = 0.0;\n"
        "    vec2 seed = floor(uv)-rand2(vec2(s));\n"
        "    uv = fract(uv);\n"
        "    for (int i = 0; i < 4; i++) { // number of strokes\n"
        "        vec2 posA = rand2(floor(seed+0.5));\n"
        "        vec2 posB = rand2(floor(seed+1.5));\n"
        "        seed += 2.0;\n"
        "        // Expand the range and mod it to get a nicely distributed random number.\n"
        "        posA = fract(posA * 128.0);\n"
        "        posB = fract(posB * 128.0);\n"
        "        // Each rune touches the edge of its box on all 4 sides.\n"
        "        if (i == 0) posA.y = 0.0;\n"
        "        if (i == 1) posA.x = 0.999;\n"
        "        if (i == 2) posA.x = 0.0;\n"
        "        if (i == 3) posA.y = 0.999;\n"
        "        // Snap the random line endpoints to a 2x3 grid (+0.5 centers in a cell).\n"
        "        vec2 snaps = vec2(2.0, 3.0);\n"
        "        posA = (floor(posA * snaps) + 0.5) / snaps;\n"
        "        posB = (floor(posB * snaps) + 0.5) / snaps;\n"
        "        // Dots (degenerate lines) are not cross-GPU safe without the 0.001 offset.\n"
        "        finalLine = max(finalLine, rune_thick_line(uv, posA, posB + 0.001));\n"
        "    }\n"
        "    return finalLine;\n"
        "}\n";
    add_float(d, "columns", "Size X", 4.0f, 2.0f, 32.0f, 1.0f);
    add_float(d, "rows",    "Size Y", 4.0f, 2.0f, 32.0f, 1.0f);
    add_output(d, Value_type::grayscale, "rune_glyph(vec2($columns, $rows)*$uv, float($seed))");
    return d;
}

// Roman numerals - ported from Material Maker roman_numerals.mmg (MIT). Draws
// a number 1..40 as I / V / X / L strokes. sdLine comes from Material Maker's
// sdline2 include. Two Godot-isms are fixed for glslang: clamp(x, 0, 1) takes
// float literals, and the C-style brace initializer for the value table is
// written as an explicit array constructor.
auto build_roman_numerals() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "roman_numerals";
    d.label    = "Roman Numerals";
    d.category = "Patterns";
    d.global =
        "vec2 rn_sd_line(vec2 p, vec2 a, vec2 b) {\n"
        "    vec2 pa = p-a, ba = b-a;\n"
        "    float h = clamp(dot(pa, ba)/dot(ba, ba), 0.0, 1.0);\n"
        "    return vec2(length(pa-ba*h), h);\n"
        "}\n"
        "\n"
        "float rn_line(vec2 p, vec4 l) { return rn_sd_line(p, l.xy, l.zw).x; }\n"
        "\n"
        "float rn_i(vec2 p, float w, float h) {\n"
        "    float d = rn_line(abs(p), vec4(w*1.7, h, 0.0, h));\n"
        "    return min(d, rn_line(vec2(p.x, abs(p.y)), vec4(0.0, h, 0.0, 0.0)));\n"
        "}\n"
        "\n"
        "float rn_v(vec2 p, float w, float h) {\n"
        "    p.x = abs(p.x);\n"
        "    float d = rn_line(p, vec4(0.0, h, w, -h));\n"
        "    return min(d, rn_line(vec2(abs(p.x-w), p.y+h), vec4(w*0.8, 0.0, 0.0, 0.0)));\n"
        "}\n"
        "\n"
        "float rn_x(vec2 p, float w, float h) {\n"
        "    p = abs(p);\n"
        "    float d = rn_line(p, vec4(w, h, 0.0, 0.0));\n"
        "    return min(d, rn_line(abs(p-vec2(w, h)), vec4(w*0.8, 0.0, 0.0, 0.0)));\n"
        "}\n"
        "\n"
        "float rn_l(vec2 p, float w, float h) {\n"
        "    float w2 = w*1.5;\n"
        "    float d = rn_line(p, vec4(-w2, h, -w2, -h));\n"
        "    return min(d, rn_line(p, vec4(-w2, h, w2, h)));\n"
        "}\n"
        "\n"
        "float rn_num(vec2 p, float w, float h, float s, int n) {\n"
        "    vec2 s0 = vec2(s, 0.0);\n"
        "    float rn[6] = float[6](\n"
        "        min(rn_x(p, w, h), rn_l(p-s0, w, h)),\n"
        "        rn_x(p, w, h),\n"
        "        min(rn_i(p, w, h), rn_x(p-s0, w, h)),\n"
        "        rn_v(p, w, h),\n"
        "        min(rn_i(p, w, h), rn_v(p-s0, w, h)),\n"
        "        rn_i(p, w, h));\n"
        "    return rn[n];\n"
        "}\n"
        "\n"
        "float rn_roman(vec2 p, float w, float h, int n, float bevel, float r, float s) {\n"
        "    p -= 0.5;\n"
        "    h *= 0.5;\n"
        "    w *= 0.25;\n"
        "    const int val[6] = int[6](40, 10, 9, 5, 4, 1);\n"
        "    float res = 1.0;\n"
        "    n = clamp(n, 0, 40);\n"
        "    for (int i = 0; i < 6; ++i) {\n"
        "        while (n - val[i] >= 0) {\n"
        "            float no = rn_num(p, w, h, s, i);\n"
        "            res = min(res, no);\n"
        "            p -= vec2(s, 0.0);\n"
        "            n -= val[i];\n"
        "        }\n"
        "    }\n"
        "    return clamp(0.0-(res-r*0.03)/max(bevel, 1e-4), 0.0, 1.0);\n"
        "}\n";
    add_input(d, "bevel_map", Value_type::grayscale, "1.0");
    // Defaults are the .mmg instance values, not the shader_model ones: the
    // latter set bevel to 0.5, which divides the 0.015-wide stroke by a 0.5
    // ramp and leaves a peak output of 0.03 - a glyph too faint to see.
    add_float(d, "w",     "Glyph Width",     0.5f,  0.0f,  1.0f, 0.01f);
    add_float(d, "h",     "Glyph Height",    0.75f, 0.0f,  1.0f, 0.01f);
    add_float(d, "r",     "Glyph Thickness", 0.4f,  0.0f,  1.0f, 0.01f);
    add_float(d, "n",     "Number",          5.0f,  1.0f, 40.0f, 1.0f);
    add_float(d, "bevel", "Bevel",           0.0f,  0.0f,  1.0f, 0.01f);
    add_float(d, "s",     "Spacing",         0.5f,  0.0f,  1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "rn_roman($uv, $w, $h, int($n), $bevel*$bevel_map($uv), $r, $s)"
    );
    return d;
}

// Seven segment display - ported from Material Maker seven_segment.mmg (MIT).
// Draws a multi-digit seven-segment number. Material Maker names the helpers
// m / fs / ssd / ssd_multi; they are prefixed ss_ here.
auto build_seven_segment() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "seven_segment";
    d.label    = "Seven Segment";
    d.category = "Patterns";
    d.global =
        "float ss_max8(vec4 a, vec4 b) {\n"
        "    return max(max(max(a.x, a.y), max(a.z, a.w)), max(max(b.x, b.y), max(b.z, b.w)));\n"
        "}\n"
        "\n"
        "vec4 ss_segments(vec2 p, float s, float st, float sl, bool is_top) {\n"
        "    sl = (1.0 - sl)/2.0;\n"
        "    st /= 2.0;\n"
        "    sl = clamp(sl, 0.0, 1.0);\n"
        "\n"
        "    p = 2.5*(1.0/s)*(p - 0.5)-vec2(0.0, is_top ? -0.5 : 0.5);\n"
        "    vec2 p1 = p;\n"
        "    vec2 p2 = p;\n"
        "\n"
        "    p = abs(p)-0.5;\n"
        "    vec4 d = vec4(min(st - abs(max(p.x, p.y) * -1.0),\n"
        "        min(-dot(p, normalize(vec2(1.0, 1.0))),\n"
        "        abs(dot(p, normalize(vec2(1.0, -1.0))))) - sl));\n"
        "\n"
        "    p1.x = abs(p1.x);\n"
        "    p2.y = abs(p2.y);\n"
        "\n"
        "    float q1 = -dot(p1, vec2(1.0, 1.0));\n"
        "    float q2 = -dot(p2, vec2(-1.0, 1.0));\n"
        "    float q3 = dot(p1, vec2(-1.0, 1.0));\n"
        "    float q4 = -dot(p2, vec2(1.0, 1.0));\n"
        "\n"
        "    return min(d, vec4(q1, q2, q3, q4));\n"
        "}\n"
        "\n"
        "float ss_digit(vec2 p, float s, float st, float sl, float bevel, int num) {\n"
        "    vec4 a = ss_segments(p, s, st, sl, true);\n"
        "    vec4 b = ss_segments(p, s, st, sl, false);\n"
        "\n"
        "    float d = 0.0;\n"
        "    vec4 x = vec4(0.0);\n"
        "\n"
        "    if (num == 0) d = ss_max8(vec4(a.xyw, b.y), vec4(b.zw, x.xx));\n"
        "    if (num == 1) d = ss_max8(vec4(a.y, b.y, x.xx), x);\n"
        "    if (num == 2) d = ss_max8(vec4(a.xyz, b.w), vec4(b.z, x.xxx));\n"
        "    if (num == 3) d = ss_max8(vec4(a.xyz, b.y), vec4(b.z, x.xxx));\n"
        "    if (num == 4) d = ss_max8(vec4(a.yzw, b.y), x);\n"
        "    if (num == 5) d = ss_max8(vec4(a.xwz, b.y), vec4(b.z, x.xxx));\n"
        "    if (num == 6) d = ss_max8(vec4(a.xwz, b.y), vec4(b.zw, x.xx));\n"
        "    if (num == 7) d = ss_max8(vec4(a.xy, b.y, x.x), x);\n"
        "    if (num == 8) d = ss_max8(a, b);\n"
        "    if (num == 9) d = ss_max8(a, vec4(b.yz, x.xx));\n"
        "\n"
        "    return clamp(d/max(bevel, 0.00001), 0.0, 1.0);\n"
        "}\n"
        "\n"
        "float ss_number(vec2 p, float s, float st, float sl, float bevel, int num, int digits, float spacing) {\n"
        "    float v = 0.0;\n"
        "    while (num > 0 || digits > 0) {\n"
        "        v = max(v, ss_digit(p, s, st, sl, bevel, num % 10));\n"
        "        num /= 10;\n"
        "        p.x += (spacing+0.5)*s;\n"
        "        digits -= 1;\n"
        "    }\n"
        "    return v;\n"
        "}\n";
    add_input(d, "bevel_map", Value_type::grayscale, "1.0");
    add_float(d, "st",      "Thickness", 0.3f, 0.0f,   1.0f, 0.01f);
    add_float(d, "sl",      "Length",    0.9f, 0.0f,   1.0f, 0.01f);
    add_float(d, "n",       "Number",    8.0f, 0.0f, 100.0f, 1.0f);
    add_float(d, "digits",  "Digits",    1.0f, 1.0f,   9.0f, 1.0f);
    add_float(d, "s",       "Scale",     1.0f, 0.0f,   1.0f, 0.01f);
    add_float(d, "bevel",   "Bevel",     0.0f, 0.0f,   1.0f, 0.01f);
    add_float(d, "spacing", "Spacing",   0.0f, 0.0f,   0.5f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "ss_number($uv, $s, $st, $sl, $bevel*$bevel_map($uv), int($n), int($digits), $spacing)"
    );
    return d;
}

// Scratches - ported from Material Maker scratches.mmg (MIT). Layered thin
// wavy strokes, each layer offset by a chained rand2 of the seed.
auto build_scratches() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "scratches";
    d.label    = "Scratches";
    d.category = "Patterns";
    d.global =
        "float scratch_one(vec2 uv, vec2 size, float waviness, float angle, float randomness, vec2 seed) {\n"
        "    float subdivide = floor(1.0/size.x);\n"
        "    float cut = size.x*subdivide;\n"
        "    uv *= subdivide;\n"
        "    vec2 r1 = rand2(floor(uv)+seed);\n"
        "    vec2 r2 = rand2(r1);\n"
        "    uv = fract(uv);\n"
        "    vec2 border = 10.0*min(fract(uv), 1.0-fract(uv));\n"
        "    uv = 2.0*uv-vec2(1.0);\n"
        "    float a = 6.28318530718*(angle+(r1.x-0.5)*randomness);\n"
        "    float c = cos(a);\n"
        "    float s = sin(a);\n"
        "    uv = vec2(c*uv.x+s*uv.y, s*uv.x-c*uv.y);\n"
        "    uv.y += 2.0*r1.y-1.0;\n"
        "    uv.y += 0.5*waviness*cos(2.0*uv.x+6.28318530718*r2.y);\n"
        "    uv.x /= cut;\n"
        "    uv.y /= subdivide*size.y;\n"
        "    return min(border.x, border.y)*(1.0-uv.x*uv.x)*max(0.0, 1.0-1000.0*uv.y*uv.y);\n"
        "}\n"
        "\n"
        "float scratch_layers(vec2 uv, int layers, vec2 size, float waviness, float angle, float randomness, vec2 seed) {\n"
        "    float v = 0.0;\n"
        "    for (int i = 0; i < layers; ++i) {\n"
        "        seed = rand2(seed);\n"
        "        v = max(v, scratch_one(fract(uv+seed), size, waviness, angle/360.0, randomness, seed));\n"
        "    }\n"
        "    return v;\n"
        "}\n";
    add_float(d, "length",     "Length",     0.25f,   0.1f,   1.0f, 0.01f);
    add_float(d, "width",      "Width",      0.5f,    0.1f,   1.0f, 0.01f);
    add_float(d, "layers",     "Layers",     4.0f,    1.0f,  10.0f, 1.0f);
    add_float(d, "waviness",   "Waviness",   0.5f,    0.0f,   1.0f, 0.01f);
    add_float(d, "angle",      "Angle",      0.0f, -180.0f, 180.0f, 1.0f);
    add_float(d, "randomness", "Randomness", 0.5f,    0.0f,   1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "scratch_layers($uv, int($layers), vec2($length, $width), $waviness, $angle, $randomness, vec2(float($seed), 0.0))"
    );
    return d;
}

// Profile - ported from Material Maker profile.mmg (MIT). Renders a gradient
// as a height profile, either as a filled area under the curve or as a curve
// of the given width. The "in" input defaults to the gradient's own luminance,
// so an unconnected node shows the edited gradient's profile.
auto build_profile() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "profile";
    d.label    = "Profile";
    d.category = "Patterns";
    d.global =
        "float draw_profile_fill(vec2 uv, float y, float dy, float w) {\n"
        "    return 1.0-clamp(sin(1.57079632679-atan(dy))*(1.0-uv.y-y)/w, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "float draw_profile_curve(vec2 uv, float y, float dy, float w) {\n"
        "    return 1.0-clamp(sin(1.57079632679-atan(dy))*abs(1.0-uv.y-y)/w, 0.0, 1.0);\n"
        "}\n";
    add_input(d, "in", Value_type::grayscale, "dot($gradient($uv.x).xyz, vec3(1.0/3.0))");
    add_enum(
        d, "style", "Style",
        {
            Enum_value{"Curve", "curve"},
            Enum_value{"Fill",  "fill"}
        },
        0
    );
    add_gradient(
        d, "gradient", "Gradient",
        {
            Gradient_stop{.position = 0.0f, .color = {0.0f, 0.0f, 0.0f, 1.0f}},
            Gradient_stop{.position = 1.0f, .color = {1.0f, 1.0f, 1.0f, 1.0f}}
        },
        Gradient_interpolation::linear
    );
    add_float(d, "width", "Width", 0.05f, 0.0f, 1.0f, 0.01f);
    add_output(
        d, Value_type::grayscale,
        "draw_profile_$(style)($uv, $in($uv), "
        "(dot($gradient($uv.x+0.001).xyz, vec3(1.0/3.0))-dot($gradient($uv.x-0.001).xyz, vec3(1.0/3.0)))/0.002, "
        "max(0.0001, $width))"
    );
    return d;
}

// Japanese glyphs - ported from Material Maker japanese_glyphs.mmg (MIT).
// Draws one hiragana / katakana glyph, or a grid of them, as quadratic bezier
// and line strokes. This is the largest port in the library: 46 glyphs per
// syllabary, each a hand-digitized stroke list. The GLSL was transcribed
// mechanically from the .mmg by a generator rather than by hand - a single
// mistyped control point would be invisible in review and would silently
// deform one glyph.
//
// Material Maker resolves its "includes" list (curve, sdline2, sdbox) against
// shared snippet nodes; those three helpers are inlined here, and every symbol
// - including Material Maker's one-letter s / l and the generic box / grid -
// carries the jg_ prefix, since erhe deduplicates globals by exact string
// match.
auto build_japanese_glyphs() -> Node_descriptor
{
    Node_descriptor d{};
    d.name     = "japanese_glyphs";
    d.label    = "Japanese Glyphs";
    d.category = "Patterns";
    d.global =
        "float jg_cross2( in vec2 a, in vec2 b ) { return a.x*b.y - a.y*b.x; }\n"
        "\n"
        "// signed distance to a quadratic bezier\n"
        "vec2 jg_sdBezier( in vec2 pos, in vec2 A, in vec2 B, in vec2 C ) {\n"
        "    vec2 a = B - A;\n"
        "    vec2 b = A - 2.0*B + C;\n"
        "    vec2 c = a * 2.0;\n"
        "    vec2 d = A - pos;\n"
        "\n"
        "    float kk = 1.0/dot(b,b);\n"
        "    float kx = kk * dot(a,b);\n"
        "    float ky = kk * (2.0*dot(a,a)+dot(d,b))/3.0;\n"
        "    float kz = kk * dot(d,a);\n"
        "\n"
        "    float res = 0.0;\n"
        "    float sgn = 0.0;\n"
        "\n"
        "    float p = ky - kx*kx;\n"
        "    float p3 = p*p*p;\n"
        "    float q = kx*(2.0*kx*kx - 3.0*ky) + kz;\n"
        "    float h = q*q + 4.0*p3;\n"
        "    float rvx;\n"
        "\n"
        "    if( h>=0.0 ) { // 1 root\n"
        "        h = sqrt(h);\n"
        "        vec2 x = (vec2(h,-h)-q)/2.0;\n"
        "        vec2 uv = sign(x)*pow(abs(x), vec2(1.0/3.0));\n"
        "        rvx = uv.x+uv.y-kx;\n"
        "        float t = clamp(rvx, 0.0, 1.0);\n"
        "        vec2 q2 = d+(c+b*t)*t;\n"
        "        res = dot(q2, q2);\n"
        "        sgn = jg_cross2(c+2.0*b*t, q2);\n"
        "    } else {   // 3 roots\n"
        "        float z = sqrt(-p);\n"
        "        float v = acos(q/(p*z*2.0))/3.0;\n"
        "        float m = cos(v);\n"
        "        float n = sin(v)*1.732050808;\n"
        "        vec3  t = clamp(vec3(m+m,-n-m,n-m)*z-kx, 0.0, 1.0);\n"
        "        vec2  qx=d+(c+b*t.x)*t.x; float dx=dot(qx, qx), sx = jg_cross2(c+2.0*b*t.x,qx);\n"
        "        vec2  qy=d+(c+b*t.y)*t.y; float dy=dot(qy, qy), sy = jg_cross2(c+2.0*b*t.y,qy);\n"
        "        if( dx<dy ) { res=dx; sgn=sx; rvx = t.x; } else { res=dy; sgn=sy; rvx = t.y; }\n"
        "    }\n"
        "\n"
        "    return vec2(rvx, sqrt(res)*sign(sgn));\n"
        "}\n"
        "\n"
        "vec2 jg_sdLine(vec2 p, vec2 a, vec2 b) {\n"
        "    vec2 pa = p-a, ba = b-a;\n"
        "    float h = clamp(dot(pa,ba)/dot(ba,ba), 0.0, 1.0);\n"
        "    return vec2(length(pa-ba*h), h);\n"
        "}\n"
        "\n"
        "float jg_sd_box(vec2 uv, vec2 size) {\n"
        "    vec2 d = abs(uv)-size;\n"
        "    return length(max(d, vec2(0)))+min(max(d.x, d.y), 0.0);\n"
        "}\n"
        "float jg_s(vec2 uv, vec2 A, vec2 B, vec2 C) { return abs(jg_sdBezier(uv, A, B, C).y); }\n"
        "float jg_l(vec2 uv, vec2 A, vec2 B) { return jg_sdLine(uv, A, B).x; }\n"
        "float jg_box(vec2 p, float r) { p = abs(p) - r; return step(max(p.x,p.y),0.0); }\n"
        "\n"
        "vec3 jg_grid(vec2 p, int gs) {\n"
        "    float g = float(gs);\n"
        "    p += 0.5;\n"
        "    vec2 a = floor(p*g)/vec2(g);\n"
        "    p = fract(p*g) - 0.5;\n"
        "    float c = g*(a.x+floor(1.0/g)) + a.y*g*g;\n"
        "    return vec3(p,c);\n"
        "}\n"
        "\n"
        "// Katakana\n"
        "\n"
        "float jg_k1(vec2 uv) { // a\n"
        "    float d = jg_s(uv, vec2(0.084, -0.004), vec2(0.399, -0.195),vec2(0.26, -0.28));\n"
        "    d = min(d, jg_s(uv, vec2(-0.278, 0.318), vec2(-0.009, 0.227),vec2(-0.021, -0.133)));\n"
        "    return min(d, jg_l(uv, vec2(-0.31, -0.28), vec2(0.26, -0.28)));\n"
        "}\n"
        "\n"
        "float jg_k2(vec2 uv) { // i\n"
        "    float d = jg_s(uv, vec2(0.264, -0.319), vec2(0, -0.051),vec2(-0.309, 0.045));\n"
        "    return min(d, jg_l(uv, vec2(0.0385, 0.3155), vec2(0.0385, -0.126)));\n"
        "}\n"
        "\n"
        "float jg_k3(vec2 uv) { // u\n"
        "    float d = jg_s(uv, vec2(0.289, -0.21), vec2(0.297, 0.234),vec2(-0.117, 0.324));\n"
        "    d = min(d, jg_l(uv, vec2(0, -0.21), vec2(0, -0.3281)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.28, -0.21), vec2(0.289, -0.21)));\n"
        "    return min(d, jg_l(uv, vec2(-0.28, 0), vec2(-0.28, -0.21)));\n"
        "}\n"
        "\n"
        "float jg_k4(vec2 uv) { // e\n"
        "    float d = jg_l(uv, vec2(0.28, -0.25), vec2(-0.28, -0.25));\n"
        "    d = min(d, jg_l(uv, vec2(0, -0.25), vec2(0, 0.25)));\n"
        "    return min(d, jg_l(uv, vec2(0.34, 0.25), vec2(-0.34, 0.25)));\n"
        "}\n"
        "\n"
        "float jg_k5(vec2 uv) { // o\n"
        "    float d = jg_s(uv, vec2(0.09, -0.19), vec2(-0.013, 0.065),vec2(-0.325, 0.228));\n"
        "    d = min(d, jg_l(uv, vec2(0.12, -0.33), vec2(0.12, 0.33)));\n"
        "    return min(d, jg_l(uv, vec2(-0.31, -0.19), vec2(0.31, -0.19)));\n"
        "}\n"
        "\n"
        "float jg_k6(vec2 uv) { // ka\n"
        "    float d = jg_s(uv, vec2(-0.039, -0.334136), vec2(0.006, 0.105),vec2(-0.319, 0.302));\n"
        "    d = min(d, jg_l(uv, vec2(-0.29, -0.19), vec2(0.28, -0.19)));\n"
        "    d = min(d, jg_s(uv, vec2(0.246, 0.217), vec2(0.231, 0.325),vec2(0.054, 0.28)));\n"
        "    return min(d, jg_l(uv, vec2(0.2469, 0.2153), vec2(0.28, -0.19)));\n"
        "}\n"
        "\n"
        "float jg_k7(vec2 uv) { // ki\n"
        "    float d = jg_l(uv, vec2(0.07, 0.33), vec2(-0.07, -0.33));\n"
        "    d = min(d, jg_l(uv, vec2(0.25, -0.23), vec2(-0.31, -0.14)));\n"
        "    return min(d, jg_l(uv, vec2(0.31, 0), vec2(-0.33, 0.1)));\n"
        "}\n"
        "\n"
        "float jg_k8(vec2 uv) { // ku\n"
        "    float d = jg_s(uv, vec2(-0.212, 0.319), vec2(0.281, 0.158),vec2(0.259, -0.22));\n"
        "    d = min(d, jg_s(uv, vec2(-0.053, -0.328), vec2(-0.141, -0.11),vec2(-0.295, -0.023)));\n"
        "    return min(d, jg_l(uv, vec2(0.2566, -0.22), vec2(-0.105, -0.2193)));\n"
        "}\n"
        "\n"
        "float jg_k9(vec2 uv) { // ke\n"
        "    float d = jg_s(uv, vec2(-0.192, 0.319), vec2(0.136, 0.167),vec2(0.128, -0.189));\n"
        "    d = min(d, jg_s(uv, vec2(-0.124, -0.332), vec2(-0.198, -0.131),vec2(-0.336, -0.01)));\n"
        "    return min(d, jg_l(uv, vec2(0.3279, -0.19), vec2(-0.19, -0.19)));\n"
        "}\n"
        "\n"
        "float jg_k10(vec2 uv) { // ko\n"
        "    float d = jg_l(uv, vec2(0.25, -0.24), vec2(-0.27, -0.24));\n"
        "    d = min(d, jg_l(uv, vec2(0.25, -0.24), vec2(0.25, 0.25)));\n"
        "    return min(d, jg_l(uv, vec2(-0.28, 0.25), vec2(0.25, 0.25)));\n"
        "}\n"
        "\n"
        "float jg_k11(vec2 uv) { // sa\n"
        "    float d = jg_l(uv, vec2(-0.342, -0.1604), vec2(0.34, -0.16));\n"
        "    d = min(d, jg_l(uv, vec2(-0.17, -0.33), vec2(-0.17, 0.0844)));\n"
        "    d = min(d, jg_s(uv, vec2(0.17, -0.068), vec2(0.176, 0.287),vec2(-0.11, 0.315)));\n"
        "    return min(d, jg_l(uv, vec2(0.17, -0.33), vec2(0.17, -0.03)));\n"
        "}\n"
        "\n"
        "float jg_k12(vec2 uv) { // shi\n"
        "    float d = jg_s(uv, vec2(-0.22, -0.306), vec2(-0.117, -0.267),vec2(-0.03, -0.204));\n"
        "    d = min(d, jg_s(uv, vec2(0.313, -0.153), vec2(0.177, 0.267),vec2(-0.273, 0.299)));\n"
        "    return min(d, jg_s(uv, vec2(-0.289, -0.098), vec2(-0.154, -0.05),vec2(-0.087, 0.013)));\n"
        "}\n"
        "\n"
        "float jg_k13(vec2 uv) { // su\n"
        "    float d = jg_s(uv, vec2(0.057, 0.044), vec2(0.195, 0.126),vec2(0.314, 0.286));\n"
        "    d = min(d, jg_s(uv, vec2(0.221, -0.269), vec2(0.1, 0.14),vec2(-0.316, 0.288)));\n"
        "    return min(d, jg_l(uv, vec2(-0.27, -0.27), vec2(0.22, -0.27)));\n"
        "}\n"
        "\n"
        "float jg_k14(vec2 uv) { // se\n"
        "    float d = jg_s(uv, vec2(0.252, -0.182), vec2(0.41, -0.176),vec2(0.119, 0.078));\n"
        "    d = min(d, jg_s(uv, vec2(-0.137, 0.15), vec2(-0.142, 0.352),vec2(0.278, 0.269)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.1353, -0.3247), vec2(-0.1373, 0.1509)));\n"
        "    return min(d, jg_l(uv, vec2(-0.3301, -0.0978), vec2(0.2521, -0.1818)));\n"
        "}\n"
        "\n"
        "float jg_k15(vec2 uv) { // so\n"
        "    float d = jg_s(uv, vec2(0.265, -0.28), vec2(0.257, 0.135),vec2(-0.194, 0.309));\n"
        "    return min(d, jg_s(uv, vec2(-0.276, -0.285), vec2(-0.199, -0.188),vec2(-0.148, -0.033)));\n"
        "}\n"
        "\n"
        "float jg_k16(vec2 uv) { // ta\n"
        "    float d = jg_s(uv, vec2(-0.212, 0.319), vec2(0.281, 0.158),vec2(0.259, -0.22));\n"
        "    d = min(d, jg_s(uv, vec2(-0.053, -0.328), vec2(-0.141, -0.11),vec2(-0.295, -0.023)));\n"
        "    d = min(d, jg_l(uv, vec2(0.2575, -0.22), vec2(-0.1038, -0.2193)));\n"
        "    return min(d, jg_s(uv, vec2(-0.102, -0.058), vec2(0.028, 0.025),vec2(0.126, 0.132)));\n"
        "}\n"
        "\n"
        "float jg_k17(vec2 uv) { // chi\n"
        "    float d = jg_s(uv, vec2(-0.269, -0.23), vec2(0.044, -0.237),vec2(0.265, -0.316));\n"
        "    d = min(d, jg_l(uv, vec2(0.338, -0.04), vec2(-0.3238, -0.04)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.213, 0.324), vec2(0.044, 0.229),vec2(0.04, -0.038)));\n"
        "    return min(d, jg_l(uv, vec2(0.04, -0.258), vec2(0.04, -0.0363)));\n"
        "}\n"
        "\n"
        "float jg_k18(vec2 uv) { // tsu\n"
        "    float d = jg_s(uv, vec2(-0.175, 0.296), vec2(0.228, 0.222),vec2(0.292, -0.271));\n"
        "    d = min(d, jg_s(uv, vec2(-0.071, -0.297), vec2(0, -0.144),vec2(0.013, -0.068)));\n"
        "    return min(d, jg_s(uv, vec2(-0.29, -0.253), vec2(-0.222, -0.11),vec2(-0.204, -0.032)));\n"
        "}\n"
        "\n"
        "float jg_k19(vec2 uv) { // te\n"
        "    float d = jg_l(uv, vec2(-0.25, -0.29), vec2(0.25, -0.29));\n"
        "    d = min(d, jg_l(uv, vec2(-0.33, -0.0704), vec2(0.33, -0.07)));\n"
        "    return min(d, jg_s(uv, vec2(-0.225, 0.32), vec2(0.029, 0.261),vec2(0.03, -0.068)));\n"
        "}\n"
        "\n"
        "float jg_k20(vec2 uv) { // to\n"
        "    float d = jg_s(uv, vec2(-0.159, -0.093), vec2(0.104, -0.02),vec2(0.294, 0.08));\n"
        "    return min(d, jg_l(uv, vec2(-0.16, -0.3254), vec2(-0.16, 0.32)));\n"
        "}\n"
        "\n"
        "float jg_k21(vec2 uv) { // na\n"
        "    float d = jg_s(uv, vec2(-0.252, 0.32), vec2(0.045, 0.253),vec2(0.039, -0.118));\n"
        "    d = min(d, jg_l(uv, vec2(0.039, -0.1141), vec2(0.0393, -0.33)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.32, -0.12), vec2(0.33, -0.12)));\n"
        "    return d;\n"
        "}\n"
        "\n"
        "float jg_k22(vec2 uv) { // ni\n"
        "    float d = jg_l(uv, vec2(-0.24, -0.23), vec2(0.25, -0.23));\n"
        "    d = min(d, jg_l(uv, vec2(-0.33, 0.24), vec2(0.33, 0.24)));\n"
        "    return d;\n"
        "}\n"
        "\n"
        "float jg_k23(vec2 uv) { // nu\n"
        "    float d = jg_s(uv, vec2(-0.268, 0.311), vec2(0.178, 0.145),vec2(0.266, -0.28));\n"
        "    d = min(d, jg_s(uv, vec2(-0.143, -0.092), vec2(0.125, 0.079),vec2(0.253, 0.23)));\n"
        "    return min(d, jg_l(uv, vec2(-0.2553, -0.28), vec2(0.2661, -0.28)));\n"
        "}\n"
        "\n"
        "float jg_k24(vec2 uv) { // ne\n"
        "    float d = jg_l(uv, vec2(0, -0.2201), vec2(0, -0.35));\n"
        "    d = min(d, jg_l(uv, vec2(0, 0.0031), vec2(0, 0.34)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.327, 0.144), vec2(0.134, -0.013),vec2(0.25, -0.219)));\n"
        "    d = min(d, jg_s(uv, vec2(0.333, 0.161), vec2(0.125, 0.041),vec2(0, 0.005)));\n"
        "    return min(d, jg_l(uv, vec2(-0.27, -0.22), vec2(0.25, -0.22)));\n"
        "}\n"
        "\n"
        "float jg_k25(vec2 uv) { // no\n"
        "    return jg_s(uv, vec2(-0.284, 0.284), vec2(0.19, 0.094),vec2(0.24, -0.294));\n"
        "}\n"
        "\n"
        "float jg_k26(vec2 uv) { // ha\n"
        "    uv.x = -abs(uv.x);\n"
        "    return jg_s(uv, vec2(-0.329, 0.271), vec2(-0.178, 0.12946),vec2(-0.148, -0.283));\n"
        "}\n"
        "\n"
        "float jg_k27(vec2 uv) { // hi\n"
        "    float d = jg_s(uv, vec2(0.269, 0.262), vec2(-0.208, 0.326),vec2(-0.21, 0.18));\n"
        "    d = min(d, jg_s(uv, vec2(-0.212, -0.049), vec2(0.057, -0.09),vec2(0.235, -0.186)));\n"
        "    return min(d, jg_l(uv, vec2(-0.2159, -0.3062), vec2(-0.21, 0.18)));\n"
        "}\n"
        "\n"
        "float jg_k28(vec2 uv) { // fu\n"
        "    float d = jg_s(uv, vec2(-0.193, 0.302), vec2(0.298, 0.141),vec2(0.28, -0.27));\n"
        "    return min(d, jg_l(uv, vec2(-0.28, -0.27), vec2(0.28, -0.27)));\n"
        "}\n"
        "\n"
        "float jg_k29(vec2 uv) { // he\n"
        "    float d = jg_l(uv, vec2(-0.0793, -0.222), vec2(0.3384, 0.2195));\n"
        "    return min(d, jg_l(uv, vec2(-0.0793, -0.222), vec2(-0.3366, 0.0766)));\n"
        "}\n"
        "\n"
        "float jg_k30(vec2 uv) { // ho\n"
        "    float d = jg_l(uv, vec2(0, -0.33), vec2(0, 0.32));\n"
        "    d = min(d, jg_l(uv, vec2(-0.33, -0.21), vec2(0.33, -0.21)));\n"
        "    uv.x = -abs(uv.x);\n"
        "    return min(d, jg_s(uv, vec2(-0.328, 0.249), vec2(-0.215, 0.059),vec2(-0.208, -0.047)));\n"
        "}\n"
        "\n"
        "float jg_k31(vec2 uv) { // ma\n"
        "    float d = jg_s(uv, vec2(0.31, -0.25), vec2(0.311, -0.135),vec2(-0.024, 0.136));\n"
        "    d = min(d, jg_s(uv, vec2(0.115, 0.314), vec2(-0.009, 0.136),vec2(-0.208, -0.047)));\n"
        "    return min(d, jg_l(uv, vec2(-0.32, -0.25), vec2(0.31, -0.25)));\n"
        "}\n"
        "\n"
        "float jg_k32(vec2 uv) { // mi\n"
        "    float d = jg_s(uv, vec2(-0.211, -0.303), vec2(0.055, -0.276),vec2(0.249, -0.215));\n"
        "    d = min(d, jg_s(uv, vec2(0.219, 0.019), vec2(-0.017, -0.05),vec2(-0.228, -0.072)));\n"
        "    return min(d, jg_s(uv, vec2(0.27, 0.309), vec2(-0.017, 0.211),vec2(-0.288, 0.174)));\n"
        "}\n"
        "\n"
        "float jg_k33(vec2 uv) { // mu\n"
        "    float d = jg_s(uv, vec2(-0.335, 0.265), vec2(0.068, 0.262),vec2(0.287, 0.198));\n"
        "    d = min(d, jg_s(uv, vec2(-0.244, 0.264), vec2(-0.139, 0.064),vec2(-0.021, -0.331)));\n"
        "    return min(d, jg_s(uv, vec2(0.33, 0.304), vec2(0.253, 0.09),vec2(0.128, -0.094)));\n"
        "}\n"
        "\n"
        "float jg_k34(vec2 uv) { // me\n"
        "    float d = jg_s(uv, vec2(-0.283, 0.293), vec2(0.069, 0.165),vec2(0.219, -0.315));\n"
        "    return min(d, jg_s(uv, vec2(-0.19, -0.191), vec2(0.076, -0.021),vec2(0.258, 0.211)));\n"
        "}\n"
        "\n"
        "float jg_k35(vec2 uv) { // mo\n"
        "    float d = jg_l(uv, vec2(-0.28, -0.28), vec2(0.28, -0.28));\n"
        "    d = min(d, jg_s(uv, vec2(-0.07, 0.171), vec2(-0.077, 0.351),vec2(0.32, 0.267)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.07, -0.28), vec2(-0.07, 0.17)));\n"
        "    return min(d, jg_l(uv, vec2(-0.32, -0.04), vec2(0.33, -0.04)));\n"
        "}\n"
        "\n"
        "float jg_k36(vec2 uv) { // ya\n"
        "    float d = jg_l(uv, vec2(-0.1546, -0.3368), vec2(-0.0156, 0.3389));\n"
        "    d = min(d, jg_s(uv, vec2(0.107, 0.07), vec2(0.41, -0.144),vec2(0.265, -0.222)));\n"
        "    return min(d,jg_l(uv, vec2(-0.325, -0.1285), vec2(0.2631, -0.2219)));\n"
        "}\n"
        "\n"
        "float jg_k37(vec2 uv) { // yu\n"
        "    float d = jg_s(uv, vec2(0.103, 0.239), vec2(0.216, -0.251),vec2(0.143, -0.248));\n"
        "    d = min(d, jg_l(uv, vec2(-0.26, -0.25), vec2(0.1425, -0.248)));\n"
        "    return min(d, jg_l(uv, vec2(-0.33, 0.24), vec2(0.34, 0.24)));\n"
        "}\n"
        "\n"
        "float jg_k38(vec2 p) { // yo\n"
        "    const vec2 a = vec2(0);\n"
        "    const float b = 0.25;\n"
        "    float c = abs(p.y)-0.25;\n"
        "\n"
        "    float d = length(max(vec2(abs(c),p.x-b),a));\n"
        "    float d1 = min(d, length(max(vec2(abs(p.y),p.x-b),a)));\n"
        "    d = min(d, length(max(vec2(abs(p.x-b),c),a)));\n"
        "    d = length(max(vec2(d,-p.x-0.25),a));\n"
        "    return min(d,length(max(vec2(d1,-p.x-0.22),a)));\n"
        "}\n"
        "\n"
        "float jg_k39(vec2 uv) { // ra\n"
        "    float d = jg_s(uv, vec2(0.27, -0.1), vec2(0.264, 0.221),vec2(-0.159, 0.319));\n"
        "    d = min(d, jg_l(uv, vec2(-0.3, -0.1), vec2(0.27, -0.1)));\n"
        "    return min(d, jg_l(uv, vec2(-0.23, -0.3), vec2(0.23, -0.3)));\n"
        "}\n"
        "\n"
        "float jg_k40(vec2 uv) { // ri\n"
        "    float d = jg_l(uv, vec2(-0.21, -0.31), vec2(-0.21, 0.06));\n"
        "    d = min(d, jg_s(uv, vec2(0.21, -0.07), vec2(0.218, 0.254),vec2(-0.148, 0.317)));\n"
        "    return min(d, jg_l(uv, vec2(0.21, -0.31), vec2(0.21, -0.07)));\n"
        "}\n"
        "\n"
        "float jg_k41(vec2 uv) { // ru\n"
        "    float d = jg_s(uv, vec2(0.05, 0.237), vec2(0.109, 0.416),vec2(0.354, 0.052));\n"
        "    d = min(d, jg_s(uv, vec2(-0.169, -0.301), vec2(-0.144, 0.2),vec2(-0.322, 0.317)));\n"
        "    return min(d, jg_l(uv, vec2(0.05, -0.31), vec2(0.05, 0.2371)));\n"
        "}\n"
        "\n"
        "float jg_k42(vec2 uv) { // re\n"
        "    float d = jg_s(uv, vec2(-0.21, 0.237), vec2(-0.216, 0.348),vec2(0.003, 0.26));\n"
        "    d = min(d, jg_s(uv, vec2(0, 0.261), vec2(0.171, 0.192),vec2(0.322, -0.021)));\n"
        "    return min(d, jg_l(uv, vec2(-0.21, -0.31), vec2(-0.21, 0.2371)));\n"
        "}\n"
        "\n"
        "float jg_k43(vec2 uv) { // ro\n"
        "    return abs(jg_sd_box(uv ,vec2(0.25,0.25)));\n"
        "}\n"
        "\n"
        "float jg_k44(vec2 uv) { // wa\n"
        "    float d = jg_l(uv, vec2(-0.271, -0.28), vec2(-0.271, -0.04));\n"
        "    d = min(d, jg_s(uv, vec2(0.28, -0.28), vec2(0.303, 0.193),vec2(-0.115, 0.313)));\n"
        "    return min(d, jg_l(uv, vec2(-0.271, -0.28), vec2(0.28, -0.28)));\n"
        "}\n"
        "\n"
        "float jg_k45(vec2 uv) { // wo\n"
        "    return min(jg_l(uv, vec2(-0.255, -0.058), vec2(0.25, -0.058)),jg_k28(uv));\n"
        "}\n"
        "\n"
        "float jg_k46(vec2 uv) { // n\n"
        "    float d = jg_s(uv, vec2(0.313, -0.196), vec2(0.221, 0.217),vec2(-0.267, 0.279));\n"
        "    return min(d, jg_s(uv, vec2(-0.078, -0.099), vec2(-0.14, -0.181),vec2(-0.273, -0.268)));\n"
        "}\n"
        "\n"
        "// Hiragana\n"
        "\n"
        "float jg_h1(vec2 uv) { // a\n"
        "    float d = jg_s(uv, vec2(-0.175, 0.287), vec2(-0.318, 0.318),vec2(-0.306, 0.19));\n"
        "    d = min(d, jg_s(uv, vec2(-0.306, 0.189), vec2(-0.266, -0.062),vec2(0.044, -0.082)));\n"
        "    d = min(d, jg_s(uv, vec2(0.044, -0.082), vec2(0.264, -0.083),vec2(0.297, 0.071)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.175, 0.287), vec2(0.013, 0.225),vec2(0.105, -0.135)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.292, -0.237), vec2(0.022, -0.228),vec2(0.243, -0.266)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.097, -0.348), vec2(-0.131, 0.113),vec2(-0.035, 0.28)));\n"
        "    return min(d, jg_s(uv, vec2(0.033, 0.333), vec2(0.333, 0.299),vec2(0.297, 0.071)));\n"
        "}\n"
        "\n"
        "float jg_h2(vec2 uv) { // i\n"
        "    float d = jg_s(uv, vec2(-0.121, 0.271), vec2(-0.309, 0.317),vec2(-0.292, -0.274));\n"
        "    d = min(d, jg_s(uv, vec2(0.319, 0.198), vec2(0.304, -0.062),vec2(0.175, -0.231)));\n"
        "    return min(d, jg_s(uv, vec2(-0.121, 0.271), vec2(-0.037, 0.246),vec2(-0.028, 0.084)));\n"
        "}\n"
        "\n"
        "float jg_h3(vec2 uv) { // u\n"
        "    float d = jg_s(uv, vec2(0.217, 0.143), vec2(0.12, 0.291),vec2(-0.129, 0.314));\n"
        "    d = min(d, jg_s(uv, vec2(0.198, -0.084), vec2(0.306, -0.01),vec2(0.218, 0.141)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.195, -0.327), vec2(-0.037, -0.298),vec2(0.175, -0.283)));\n"
        "    return min(d, jg_s(uv, vec2(0.197, -0.085), vec2(0.072, -0.162),vec2(-0.274, -0.065)));\n"
        "}\n"
        "\n"
        "float jg_h4(vec2 uv) { // e\n"
        "    float d = jg_s(uv, vec2(0.093, 0.171), vec2(0.101, 0.386),vec2(0.345, 0.277));\n"
        "    d = min(d, jg_s(uv, vec2(0.093, 0.17), vec2(0.081, -0.032),vec2(-0.311, 0.298)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.173, -0.337), vec2(-0.037, -0.298),vec2(0.175, -0.283)));\n"
        "    d = min(d, jg_s(uv, vec2(0.165, -0.151), vec2(0.065, -0.14),vec2(-0.253, -0.119)));\n"
        "    return min(d, jg_l(uv, vec2(0.1651, -0.1503), vec2(-0.3101, 0.2973)));\n"
        "}\n"
        "\n"
        "float jg_h5(vec2 uv) { // o\n"
        "    float d = jg_s(uv, vec2(-0.103, 0.022), vec2(0.111, -0.047),vec2(0.23, 0.027));\n"
        "    d = min(d, jg_s(uv, vec2(0.23, 0.027), vec2(0.357, 0.119),vec2(0.263, 0.249)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.286, 0.265), vec2(-0.414, 0.155),vec2(-0.104, 0.022)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.12, 0.192), vec2(-0.121, 0.383),vec2(-0.286, 0.265)));\n"
        "    d = min(d, jg_s(uv, vec2(0.262, 0.25), vec2(0.158, 0.376),vec2(0.033, 0.244)));\n"
        "    d = min(d,jg_s(uv, vec2(-0.305, -0.181), vec2(-0.074, -0.182),vec2(0.073, -0.214)));\n"
        "    d = min(d, jg_s(uv, vec2(0.337, -0.131), vec2(0.27, -0.219),vec2(0.169, -0.277)));\n"
        "    return min(d, jg_l(uv, vec2(-0.12, -0.3504), vec2(-0.12, 0.19)));\n"
        "}\n"
        "\n"
        "float jg_h6(vec2 uv) { // ka\n"
        "    float d = jg_s(uv, vec2(-0.342, -0.13), vec2(-0.206, -0.147),vec2(-0.112, -0.156));\n"
        "    d = min(d, jg_s(uv, vec2(0.094, -0.046), vec2(0.082, -0.181),vec2(-0.11, -0.156)));\n"
        "    d = min(d, jg_s(uv, vec2(0.06, 0.239), vec2(0.107, 0.114),vec2(0.094, -0.045)));\n"
        "    d = min(d, jg_s(uv, vec2(0.06, 0.239), vec2(0.02, 0.345),vec2(-0.145, 0.271)));\n"
        "    d = min(d, jg_s(uv, vec2(0.34, 0.083), vec2(0.274, -0.103),vec2(0.176, -0.235)));\n"
        "    return min(d, jg_s(uv, vec2(-0.104, -0.341), vec2(-0.184, 0.092),vec2(-0.326, 0.304)));\n"
        "}\n"
        "\n"
        "float jg_h7(vec2 uv) { // ki\n"
        "    float d = jg_s(uv, vec2(-0.048, -0.349), vec2(0.091, -0.023),vec2(0.239, 0.124));\n"
        "    d = min(d, jg_s(uv, vec2(-0.149, 0.087), vec2(0.033, 0.036),vec2(0.239, 0.124)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.241, 0.176), vec2(-0.237, 0.116),vec2(-0.149, 0.087)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.291, -0.051), vec2(-0.031, -0.05),vec2(0.294, -0.127)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.29, -0.209), vec2(0.027, -0.217),vec2(0.239, -0.278)));\n"
        "    return min(d, jg_s(uv, vec2(0.201, 0.288), vec2(-0.252, 0.38),vec2(-0.241, 0.176)));\n"
        "}\n"
        "\n"
        "float jg_h8(vec2 uv) { // ku\n"
        "    float d = jg_s(uv, vec2(0.147, -0.329), vec2(0.018, -0.207),vec2(-0.186, -0.07));\n"
        "    d = min(d, jg_s(uv, vec2(-0.186, -0.07), vec2(-0.254, -0.02),vec2(-0.191, 0.025)));\n"
        "    return min(d, jg_s(uv, vec2(-0.19, 0.026), vec2(0.028947, 0.168335),vec2(0.183612, 0.325139)));\n"
        "}\n"
        "\n"
        "float jg_h9(vec2 uv) { // ke\n"
        "    float d = jg_s(uv, vec2(0.183, -0.332), vec2(0.265, 0.268),vec2(-0.009, 0.328));\n"
        "    d = min(d, jg_s(uv, vec2(-0.099, -0.136), vec2(0.076, -0.12),vec2(0.332, -0.158)));\n"
        "    return min(d, jg_s(uv, vec2(-0.229, -0.323), vec2(-0.309, 0.089),vec2(-0.235, 0.298)));\n"
        "}\n"
        "\n"
        "float jg_h10(vec2 uv) { // ko\n"
        "    float d = jg_s(uv, vec2(-0.225, -0.272), vec2(0.00429, -0.252796),vec2(0.208268, -0.268477));\n"
        "    d = min(d, jg_s(uv, vec2(-0.002435, -0.145273), vec2(0.093951, -0.205755),vec2(0.208268, -0.266237)));\n"
        "    return min(d, jg_s(uv, vec2(0.266548, 0.255698), vec2(-0.452981, 0.36098),vec2(-0.195206, 0.009291)));\n"
        "}\n"
        "\n"
        "float jg_h11(vec2 uv) { // sa\n"
        "    float d = jg_s(uv, vec2(-0.048, -0.349), vec2(0.091, -0.023),vec2(0.239, 0.124));\n"
        "    d = min(d, jg_s(uv, vec2(-0.149, 0.087), vec2(0.033, 0.036),vec2(0.239, 0.124)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.241, 0.176), vec2(-0.237, 0.116),vec2(-0.149, 0.087)));\n"
        "    d = min(d,jg_s(uv, vec2(-0.300558, -0.165434), vec2(-0.011401, -0.149753),vec2(0.275514, -0.234876)));\n"
        "    return min(d, jg_s(uv, vec2(0.201, 0.288), vec2(-0.252, 0.38),vec2(-0.241, 0.176)));\n"
        "}\n"
        "\n"
        "float jg_h12(vec2 uv) { // shi\n"
        "    float d = jg_s(uv, vec2(-0.068, 0.306), vec2(-0.201, 0.289),vec2(-0.2, 0.072));\n"
        "    d = min(d, jg_s(uv, vec2(-0.065, 0.306), vec2(0.201, 0.343),vec2(0.306895, 0.038412)));\n"
        "    return min(d, jg_l(uv, vec2(-0.2, 0.07), vec2(-0.19, -0.33)));\n"
        "}\n"
        "\n"
        "float jg_h13(vec2 uv) { // su\n"
        "    float d = jg_s(uv, vec2(0.08, -0.056), vec2(0.076019, 0.125774),vec2(-0.051748, 0.136974));\n"
        "    d = min(d, jg_s(uv, vec2(-0.053, 0.137), vec2(-0.191, 0.142),vec2(-0.18, 0.004)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.18, 0.004), vec2(-0.172, -0.057),vec2(-0.126, -0.082)));\n"
        "    d = min(d, jg_s(uv, vec2(0.044, -0.048), vec2(-0.038, -0.126),vec2(-0.124, -0.083)));\n"
        "    d = min(d, jg_s(uv, vec2(0.04, -0.052), vec2(0.124, 0.039),vec2(0.088, 0.146)));\n"
        "    d = min(d, jg_s(uv, vec2(0.087226, 0.148175), vec2(0.046, 0.276),vec2(-0.149, 0.316)));\n"
        "    d = min(d,jg_s(uv, vec2(0.336035, -0.230396), vec2(-0.002435, -0.234876),vec2(-0.340905, -0.214715)));\n"
        "    return min(d, jg_l(uv, vec2(0.08, -0.0559), vec2(0.08, -0.35)));\n"
        "}\n"
        "\n"
        "float jg_h14(vec2 uv) { // se\n"
        "    float d = jg_s(uv, vec2(0.353967, -0.147513), vec2(0.015497, -0.136313),vec2(-0.352113, -0.113912));\n"
        "    d = min(d, jg_s(uv, vec2(-0.18, 0.163), vec2(-0.179515, 0.3565),vec2(0.282239, 0.264658)));\n"
        "    d = min(d, jg_s(uv, vec2(0.146, 0.08), vec2(0.188095, 0.027211),vec2(0.179129, -0.328958)));\n"
        "    d = min(d, jg_s(uv, vec2(0.146, 0.08), vec2(0.097, 0.135),vec2(-0.018126, 0.072013)));\n"
        "    return min(d, jg_l(uv, vec2(-0.18, -0.31), vec2(-0.18, 0.16)));\n"
        "}\n"
        "\n"
        "float jg_h15(vec2 uv) { // so\n"
        "    float d = jg_s(uv, vec2(0.174646, -0.31), vec2(-0.015884, -0.291),vec2(-0.215, -0.299));\n"
        "    d = min(d, jg_s(uv, vec2(0.174646, -0.31), vec2(-0.097, -0.062),vec2(-0.332, -0.006)));\n"
        "    d = min(d, jg_s(uv, vec2(0.33, -0.062), vec2(0.042, -0.057),vec2(-0.332, -0.006)));\n"
        "    d = min(d, jg_s(uv, vec2(0.209, 0.309459), vec2(-0.077, 0.313),vec2(-0.068, 0.163)));\n"
        "    return min(d, jg_s(uv, vec2(-0.068, 0.162), vec2(-0.056231, 0.013771),vec2(0.33, -0.062)));\n"
        "}\n"
        "\n"
        "float jg_h16(vec2 uv) { // ta\n"
        "    float d = jg_s(uv, vec2(0.067052, -0.223675), vec2(-0.139168, -0.190074),vec2(-0.331939, -0.194555));\n"
        "    d = min(d, jg_s(uv, vec2(-0.309524, 0.296019), vec2(-0.192964, 0.058572),vec2(-0.114511, -0.353599)));\n"
        "    d = min(d, jg_s(uv, vec2(0.291205, -0.104952), vec2(0.129815, -0.111672),vec2(0.008773, -0.087032)));\n"
        "    return min(d, jg_s(uv, vec2(-0.015884, 0.103373), vec2(-0.177274, 0.36098),vec2(0.322586, 0.289298)));\n"
        "}\n"
        "\n"
        "float jg_h17(vec2 uv) { // chi\n"
        "    float d = jg_s(uv, vec2(-0.237155, 0.111585), vec2(-0.124465, -0.108956),vec2(-0.054034, -0.343574));\n"
        "    d = min(d, jg_s(uv, vec2(-0.305239, -0.205149), vec2(-0.082206, -0.193419),vec2(0.255865, -0.226265)));\n"
        "    d = min(d, jg_s(uv, vec2(0.241779, 0.048238), vec2(0.061, -0.086),vec2(-0.236, 0.111)));\n"
        "    d = min(d,jg_s(uv, vec2(0.243, 0.23), vec2(0.139, 0.362),vec2(-0.194, 0.278164)));\n"
        "    return min(d, jg_s(uv, vec2(0.243, 0.23), vec2(0.323, 0.113),vec2(0.243, 0.049)));\n"
        "}\n"
        "\n"
        "float jg_h18(vec2 uv) { // tsu\n"
        "    float d = jg_s(uv, vec2(0.3, -0.072), vec2(0.237, -0.307),vec2(-0.324021, -0.165264));\n"
        "    d = min(d, jg_s(uv, vec2(0.188, 0.182), vec2(0.324, 0.099),vec2(0.300471, -0.066725)));\n"
        "    return min(d, jg_s(uv, vec2(-0.145594, 0.251), vec2(0.047, 0.253),vec2(0.186, 0.183)));\n"
        "}\n"
        "\n"
        "float jg_h19(vec2 uv) { // te\n"
        "    float d = jg_s(uv, vec2(0.326296, -0.275535), vec2(0.190129, -0.280227),vec2(-0.316978, -0.233304));\n"
        "    d = min(d, jg_s(uv, vec2(0.324, -0.274), vec2(-0.063424, -0.179),vec2(-0.071, 0.045)));\n"
        "    return min(d, jg_s(uv, vec2(-0.071, 0.048), vec2(-0.077, 0.259),vec2(0.227692, 0.292241)));\n"
        "}\n"
        "\n"
        "float jg_h20(vec2 uv) { // to\n"
        "    float d = jg_s(uv, vec2(-0.256, 0.164), vec2(-0.26298, -0.045609),vec2(0.234735, -0.160572));\n"
        "    d = min(d, jg_s(uv, vec2(0.255, 0.274), vec2(-0.235, 0.353242),vec2(-0.256, 0.166)));\n"
        "    return min(d, jg_s(uv, vec2(-0.132, -0.336536), vec2(-0.099, -0.158226),vec2(-0.062, -0.066)));\n"
        "}\n"
        "\n"
        "float jg_h21(vec2 uv) { // na\n"
        "    float d = jg_s(uv, vec2(-0.333, -0.191072), vec2(-0.183158, -0.193419),vec2(0.011702, -0.212188));\n"
        "    d = min(d, jg_s(uv, vec2(-0.325, 0.195), vec2(-0.205, 0.013),vec2(-0.118, -0.355)));\n"
        "    d = min(d, jg_s(uv, vec2(0.131436, -0.233304), vec2(0.248822, -0.188726),vec2(0.323949, -0.132418)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.028209, 0.315703), vec2(0.161956, 0.357934),vec2(0.161956, 0.20074)));\n"
        "    d = min(d,jg_s(uv, vec2(-0.032, 0.315), vec2(-0.162, 0.274),vec2(-0.098, 0.164)));\n"
        "    d = min(d, jg_s(uv, vec2(0.333, 0.259395), vec2(0.251, 0.171),vec2(0.128, 0.129)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.096292, 0.160855), vec2(-0.041, 0.079),vec2(0.128, 0.129)));\n"
        "    return min(d, jg_l(uv, vec2(0.161956, 0.198394), vec2(0.143175, -0.083148)));\n"
        "}\n"
        "\n"
        "float jg_h22(vec2 uv) { // ni\n"
        "    float d = jg_s(uv, vec2(-0.251242, 0.322742), vec2(-0.312282, 0.02243),vec2(-0.23246, -0.327151));\n"
        "    d = min(d, jg_s(uv, vec2(0.288733, -0.230957), vec2(0.086829, -0.207496),vec2(-0.063424, -0.219227)));\n"
        "    return min(d, jg_s(uv, vec2(0.324, 0.24), vec2(-0.25, 0.329),vec2(-0.032, 0.024)));\n"
        "}\n"
        "\n"
        "float jg_h23(vec2 uv) { // nu\n"
        "    float d = jg_s(uv, vec2(-0.071, 0.214817), vec2(-0.214, -0.052648),vec2(-0.230112, -0.294304));\n"
        "    d = min(d, jg_s(uv, vec2(0.070395, -0.33419), vec2(0.025789, 0.010699),vec2(-0.151, 0.224)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.284, 0.239), vec2(-0.212, 0.285),vec2(-0.152, 0.225)));\n"
        "    d = min(d,jg_s(uv, vec2(-0.04, -0.194), vec2(-0.171, -0.165264),vec2(-0.241, -0.081)));\n"
        "    d = min(d, jg_s(uv, vec2(0.284037, -0.073763), vec2(0.187781, -0.233304),vec2(-0.04, -0.194)));\n"
        "    d = min(d, jg_s(uv, vec2(0.268, 0.214), vec2(0.354, 0.082),vec2(0.285, -0.072)));\n"
        "    d = min(d, jg_s(uv, vec2(0.268, 0.214), vec2(0.163, 0.35),vec2(0.058, 0.261)));\n"
        "    d = min(d, jg_s(uv, vec2(0.056, 0.144), vec2(0.004, 0.199),vec2(0.058, 0.261)));\n"
        "    d = min(d, jg_s(uv, vec2(0.057, 0.143), vec2(0.173, 0.043),vec2(0.367, 0.271)));\n"
        "    return min(d, jg_s(uv, vec2(-0.284, 0.239), vec2(-0.415, 0.138),vec2(-0.242, -0.08)));\n"
        "}\n"
        "\n"
        "float jg_h24(vec2 uv) { // ne\n"
        "    float d = jg_s(uv, vec2(-0.183158, -0.34592), vec2(-0.20194, 0.076392),vec2(-0.194896, 0.334473));\n"
        "    d = min(d, jg_s(uv, vec2(0.183, -0.196), vec2(0.011, -0.27),vec2(-0.354, 0.189)));\n"
        "    d = min(d, jg_s(uv, vec2(0.028137, 0.287549), vec2(0.24, 0.369),vec2(0.283, 0.132)));\n"
        "    d = min(d,jg_s(uv, vec2(-0.006, 0.173), vec2(-0.043, 0.25),vec2(0.027, 0.287)));\n"
        "    d = min(d, jg_s(uv, vec2(0.153, 0.123), vec2(0.032, 0.099),vec2(-0.006, 0.173)));\n"
        "    d = min(d, jg_s(uv, vec2(0.183, -0.196), vec2(0.32, -0.135),vec2(0.283, 0.132)));\n"
        "    d = min(d, jg_s(uv, vec2(0.376, 0.28), vec2(0.301, 0.16),vec2(0.153, 0.123)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.157333, -0.193419), vec2(-0.1908, -0.1535)));\n"
        "    return min(d, jg_l(uv, vec2(-0.157333, -0.193419), vec2(-0.340455, -0.174649)));\n"
        "}\n"
        "\n"
        "float jg_h25(vec2 uv) { // no\n"
        "    float d = jg_s(uv, vec2(0.008, -0.284), vec2(-0.042536, 0.308529),vec2(-0.225215, 0.238869));\n"
        "    d = min(d, jg_s(uv, vec2(-0.31, -0.036), vec2(-0.361, 0.174012),vec2(-0.225215, 0.238869)));\n"
        "    d = min(d, jg_s(uv, vec2(0.008, -0.284), vec2(-0.226, -0.28),vec2(-0.31, -0.036)));\n"
        "    d = min(d, jg_s(uv, vec2(0.313, -0.032), vec2(0.338, 0.241),vec2(0.021, 0.298)));\n"
        "    return min(d, jg_s(uv, vec2(0.008, -0.284), vec2(0.289169, -0.275179),vec2(0.313, -0.032)));\n"
        "}\n"
        "\n"
        "float jg_h26(vec2 uv) { // ha\n"
        "    float d = jg_s(uv, vec2(-0.256402, 0.326023), vec2(-0.338065, 0.035291),vec2(-0.239389, -0.335351));\n"
        "    d = min(d, jg_s(uv, vec2(-0.113493, -0.165332), vec2(0.196145, -0.167032),vec2(0.328846, -0.184034)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.042, 0.125), vec2(0.105975, 0.042091),vec2(0.350963, 0.247815)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.066, 0.27), vec2(-0.128, 0.18),vec2(-0.042, 0.125)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.066, 0.27), vec2(0, 0.336),vec2(0.097, 0.298)));\n"
        "    d = min(d, jg_s(uv, vec2(0.179132, 0.174706), vec2(0.180833, 0.264817),vec2(0.097, 0.298)));\n"
        "    return min(d, jg_l(uv, vec2(0.16, -0.338752), vec2(0.179132, 0.174706)));\n"
        "}\n"
        "\n"
        "float jg_h27(vec2 uv) { // hi\n"
        "    float d = jg_s(uv, vec2(-0.322753, -0.262243), vec2(-0.20196, -0.255442),vec2(-0.050544, -0.297947));\n"
        "    d = min(d, jg_s(uv, vec2(-0.050544, -0.297947), vec2(-0.397, 0.085),vec2(-0.215571, 0.261416)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.215571, 0.261416), vec2(-0.142, 0.334),vec2(0.002, 0.299)));\n"
        "    d = min(d, jg_s(uv, vec2(0.128, -0.295), vec2(0.253989, -0.031017),vec2(0.354366, 0.013188)));\n"
        "    return min(d, jg_s(uv, vec2(0.002, 0.299), vec2(0.274404, 0.222),vec2(0.128, -0.295)));\n"
        "}\n"
        "\n"
        "float jg_h28(vec2 uv) { // fu\n"
        "    float d = jg_s(uv, vec2(-0.336363, 0.275018), vec2(-0.252999, 0.076095),vec2(-0.244493, -0.010615));\n"
        "    d = min(d, jg_s(uv, vec2(0.167222, -0.255442), vec2(-0.026726, -0.255442),vec2(-0.198558, -0.314949)));\n"
        "    d = min(d, jg_s(uv, vec2(0.167222, -0.255442), vec2(-0.166233, -0.14323),vec2(-0.009713, 0.009788)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.009713, 0.009788), vec2(0.235274, 0.241014),vec2(0, 0.310722)));\n"
        "    d = min(d, jg_s(uv, vec2(0, 0.310722), vec2(-0.125402, 0.334524),vec2(-0.181545, 0.237613)));\n"
        "    return min(d, jg_s(uv, vec2(0.342457, 0.261416), vec2(0.311833, 0.106699),vec2(0.219963, -0.017415)));\n"
        "}\n"
        "\n"
        "float jg_h29(vec2 uv) { // he\n"
        "    float d = jg_s(uv, vec2(-0.030129, -0.173833), vec2(-0.101583, -0.267344),vec2(-0.171337, -0.163632));\n"
        "    d = min(d, jg_s(uv, vec2(-0.030129, -0.173833), vec2(0.175729, 0.084596),vec2(0.35947, 0.225712)));\n"
        "    return min(d, jg_l(uv, vec2(-0.351675, 0.081196), vec2(-0.171337, -0.163632)));\n"
        "}\n"
        "\n"
        "float jg_h30(vec2 uv) { // ho\n"
        "    float d = jg_s(uv, vec2(-0.256402, 0.326023), vec2(-0.338065, 0.035291),vec2(-0.239389, -0.335351));\n"
        "    d = min(d, jg_s(uv, vec2(-0.042, 0.125), vec2(0.105975, 0.042091),vec2(0.350963, 0.247815)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.066, 0.27), vec2(-0.128, 0.18),vec2(-0.042, 0.125)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.066, 0.27), vec2(0, 0.336),vec2(0.097, 0.298)));\n"
        "    d = min(d, jg_s(uv, vec2(0.179132, 0.174706), vec2(0.180833, 0.264817),vec2(0.097, 0.298)));\n"
        "    d = min(d, jg_s(uv, vec2(0.310132, -0.301347), vec2(0.078755, -0.272444),vec2(-0.087973, -0.284346)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.093077, -0.085423), vec2(0.100872, -0.073522),vec2(0.318638, -0.102425)));\n"
        "    return min(d, jg_l(uv, vec2(0.16, -0.285), vec2(0.179132, 0.174706)));\n"
        "}\n"
        "\n"
        "float jg_h31(vec2 uv) { // ma\n"
        "    float d = jg_s(uv, vec2(-0.289987, -0.236059), vec2(0.088339, -0.219379),vec2(0.290021, -0.254129));\n"
        "    d = min(d, jg_s(uv, vec2(-0.281642, -0.066479), vec2(0.022967, -0.055359),vec2(0.291412, -0.088719)));\n"
        "    d = min(d, jg_s(uv, vec2(0.301148, 0.27685), vec2(0.032703, 0.053061),vec2(-0.200969, 0.121171)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.200969, 0.121171), vec2(-0.288596, 0.15592),vec2(-0.26356, 0.23237)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.26356, 0.23237), vec2(-0.214757, 0.327384),vec2(-0.076284, 0.311293)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.076284, 0.311293), vec2(0.039647, 0.283938),vec2(0.027139, 0.16426)));\n"
        "    return min(d, jg_l(uv, vec2(0.01184, -0.348649), vec2(0.027139, 0.16426)));\n"
        "}\n"
        "\n"
        "float jg_h32(vec2 uv) { // mi\n"
        "    float d = jg_s(uv, vec2(-0.05, -0.31), vec2(0.010664, -0.313037),vec2(-0.008658, -0.232582));\n"
        "    d = min(d, jg_s(uv, vec2(-0.008658, -0.232582), vec2(-0.079504, 0.081192),vec2(-0.16, 0.19061)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.16, 0.19061), vec2(-0.27, 0.309),vec2(-0.321026, 0.176129)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.321026, 0.176129), vec2(-0.36, 0.07),vec2(-0.25179, -0.005699)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.25179, -0.005699), vec2(-0.005438, -0.1473),vec2(0.353625, 0.143947)));\n"
        "    d = min(d, jg_s(uv, vec2(0.239305, -0.190745), vec2(0.242525, 0.235665),vec2(0.012274, 0.322556)));\n"
        "    return min(d, jg_l(uv, vec2(-0.27, -0.3), vec2(-0.05, -0.31)));\n"
        "}\n"
        "\n"
        "float jg_h33(vec2 uv) { // mu\n"
        "    float d = jg_s(uv, vec2(-0.303541, -0.22891), vec2(-0.141159, -0.225933),vec2(0.062935, -0.243798));\n"
        "    d = min(d, jg_s(uv, vec2(0.189563, -0.267618), vec2(0.261071, -0.212534),vec2(0.334068, -0.10832)));\n"
        "    d = min(d, jg_s(uv, vec2(0.140402, 0.302581), vec2(0.337048, 0.251962),vec2(0.222338, 0.048001)));\n"
        "    d = min(d, jg_s(uv, vec2(0.140402, 0.302581), vec2(0.003346, 0.32789),vec2(-0.13818, 0.305558)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.13818, 0.305558), vec2(-0.240972, 0.26685),vec2(-0.157547, 0.131372)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.157547, 0.131372), vec2(-0.036, -0.081522),vec2(-0.211177, -0.0845)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.211177, -0.0845), vec2(-0.332589, -0.069282),vec2(-0.316597, 0.06841)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.316597, 0.06841), vec2(-0.301834, 0.175368),vec2(-0.209569, 0.147092)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.209569, 0.147092), vec2(-0.154, 0.129),vec2(-0.126, 0.06841)));\n"
        "    return min(d, jg_l(uv, vec2(-0.133711, -0.066), vec2(-0.130731, -0.343545)));\n"
        "}\n"
        "\n"
        "float jg_h34(vec2 uv) { // me\n"
        "    float d = jg_s(uv, vec2(-0.031688, 0.203557), vec2(-0.129642, 0.119877),vec2(-0.205478, -0.312734));\n"
        "    d = min(d, jg_s(uv, vec2(0.101024, -0.341154), vec2(0.00465, 0.197241),vec2(-0.197578, 0.247765)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.197578, 0.247765), vec2(-0.32397, 0.261975),vec2(-0.319231, 0.108824)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.319231, 0.108824), vec2(-0.317651, -0.056957),vec2(-0.15966, -0.15169)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.15966, -0.15169), vec2(0.097864, -0.27642),vec2(0.262174, -0.121691)));\n"
        "    d = min(d, jg_s(uv, vec2(0.262174, -0.121691), vec2(0.377507, 0.006198),vec2(0.279553, 0.160927)));\n"
        "    return min(d, jg_s(uv, vec2(0.279553, 0.160927), vec2(0.198978, 0.280922),vec2(0.00623, 0.307762)));\n"
        "}\n"
        "\n"
        "float jg_h35(vec2 uv) { // mo\n"
        "    float d = jg_s(uv, vec2(-0.284473, -0.214844), vec2(-0.071186, -0.183267),vec2(0.131042, -0.202213));\n"
        "    d = min(d, jg_s(uv, vec2(-0.311331, -0.008012), vec2(-0.112263, 0.026723),vec2(0.115243, 0.012513)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.072766, -0.341154), vec2(-0.143861, 0.081984)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.143861, 0.081984), vec2(-0.1723, 0.329867),vec2(0.037828, 0.320393)));\n"
        "    return min(d, jg_s(uv, vec2(0.037828, 0.320393), vec2(0.402785, 0.315657),vec2(0.230576, -0.017485)));\n"
        "\n"
        "}\n"
        "\n"
        "float jg_h36(vec2 uv) { // ya\n"
        "    float d = jg_s(uv, vec2(-0.230756, -0.317471), vec2(-0.082245, 0.113561),vec2(-0.044327, 0.340919));\n"
        "    d = min(d, jg_s(uv, vec2(-0.33187, -0.069588), vec2(-0.012729, -0.227475),vec2(0.184, -0.211687)));\n"
        "    d = min(d, jg_s(uv, vec2(0.32063, -0.058536), vec2(0.344329, -0.188004),vec2(0.184, -0.211687)));\n"
        "    d = min(d, jg_s(uv, vec2(0.32063, -0.058536), vec2(0.279553, 0.148296),vec2(0.028348, 0.066195)));\n"
        "    return min(d, jg_l(uv, vec2(0.061526, -0.352206), vec2(0.069426, -0.210108)));\n"
        "}\n"
        "\n"
        "float jg_h37(vec2 uv) { // yu\n"
        "    float d = jg_s(uv, vec2(-0.287633, 0.19882), vec2(-0.320811, 0.020408),vec2(-0.270254, -0.295367));\n"
        "    d = min(d, jg_s(uv, vec2(-0.287633, 0.19882), vec2(-0.200738, -0.257474),vec2(0.131, -0.229)));\n"
        "    d = min(d, jg_s(uv, vec2(0.131, -0.229), vec2(0.344329, -0.199056),vec2(0.319051, 0.00304)));\n"
        "    d = min(d, jg_s(uv, vec2(0.319051, 0.00304), vec2(0.300092, 0.151454),vec2(0.132, 0.203)));\n"
        "    d = min(d, jg_s(uv, vec2(0.132, 0.203), vec2(-0.044327, 0.24145),vec2(-0.134382, 0.067774)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.104364, 0.342498), vec2(0.151581, 0.238292),vec2(0.052047, -0.353785)));\n"
        "    return d;\n"
        "}\n"
        "\n"
        "float jg_h38(vec2 uv) { // yo\n"
        "    float d = jg_s(uv, vec2(0.035258, 0.165025), vec2(0.004476, -0.077452),vec2(0.000854, -0.343454));\n"
        "    d = min(d, jg_s(uv, vec2(0.035258, 0.165025), vec2(0.046122, 0.284454),vec2(-0.037171, 0.298931)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.037171, 0.298931), vec2(-0.22, 0.34),vec2(-0.274375, 0.253692)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.274375, 0.253692), vec2(-0.344, 0.121),vec2(-0.13676, 0.090834)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.13676, 0.090834), vec2(0.125, 0.06),vec2(0.286947, 0.273597)));\n"
        "    return min(d, jg_s(uv, vec2(0.288758, -0.187834), vec2(0.142, -0.155),vec2(0.00666, -0.163)));\n"
        "}\n"
        "\n"
        "float jg_h39(vec2 uv) { // ra\n"
        "    float d = jg_s(uv, vec2(-0.154067, -0.319705), vec2(0.029707, -0.278893),vec2(0.181924, -0.256632));\n"
        "    d = min(d, jg_s(uv, vec2(-0.256163, 0.099545), vec2(-0.241313, -0.063703),vec2(-0.209756, -0.165733)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.256163, 0.099545), vec2(-0.085383, -0.048862),vec2(0.096534, -0.028456)));\n"
        "    d = min(d, jg_s(uv, vec2(0.096534, -0.028456), vec2(0.328, -0.001),vec2(0.263601, 0.181169)));\n"
        "    return min(d, jg_s(uv, vec2(0.263601, 0.181169), vec2(0.196, 0.344837),vec2(-0.196054, 0.288222)));\n"
        "}\n"
        "\n"
        "float jg_h40(vec2 uv) { // ri\n"
        "    float d = jg_s(uv, vec2(-0.208931, 0.109127), vec2(-0.260077, -0.097026),vec2(-0.171424, -0.345772));\n"
        "    d = min(d, jg_s(uv, vec2(-0.208931, 0.109127), vec2(-0.127098, -0.28103),vec2(0.041683, -0.279326)));\n"
        "    d = min(d, jg_s(uv, vec2(0.041683, -0.279326), vec2(0.166137, -0.279326),vec2(0.207054, -0.153249)));\n"
        "    d = min(d, jg_s(uv, vec2(0.207054, -0.153249), vec2(0.259904, 0.046088),vec2(0.174661, 0.175573)));\n"
        "    return min(d, jg_s(uv, vec2(0.174661, 0.175573), vec2(0.099648, 0.303353),vec2(-0.120278, 0.330613)));\n"
        "}\n"
        "\n"
        "float jg_h41(vec2 uv) { // ru\n"
        "    float d = jg_s(uv, vec2(0.179776, -0.309994), vec2(-0.014578, -0.291252),vec2(-0.21916, -0.298067));\n"
        "    d = min(d, jg_s(uv, vec2(0.179776, -0.309994), vec2(0.033158, -0.141323),vec2(-0.309517, 0.081867)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.309517, 0.081867), vec2(0.123516, -0.132804),vec2(0.25479, 0.022236)));\n"
        "    d = min(d, jg_s(uv, vec2(0.25479, 0.022236), vec2(0.331508, 0.109127),vec2(0.249675, 0.221574)));\n"
        "    d= min(d, jg_s(uv, vec2(0.249675, 0.221574), vec2(0.150793, 0.330613),vec2(-0.038445, 0.31528)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.038445, 0.31528), vec2(-0.209385, 0.293077),vec2(-0.168507, 0.176974)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.168507, 0.176974), vec2(-0.134084, 0.121072),vec2(-0.041571, 0.136123)));\n"
        "    d = min(d, jg_s(uv, vec2(0.25479, 0.022236), vec2(0.331508, 0.109127),vec2(0.249675, 0.221574)));\n"
        "    return min(d, jg_s(uv, vec2(-0.041571, 0.136123), vec2(0.088, 0.169),vec2(0.074607, 0.312)));\n"
        "}\n"
        "\n"
        "float jg_h42(vec2 uv) { // re\n"
        "    float d = jg_s(uv, vec2(-0.183158, -0.34592), vec2(-0.20194, 0.076392),vec2(-0.194896, 0.334473));\n"
        "    d = min(d,  jg_s(uv, vec2(0.152, -0.219), vec2(0.011, -0.27),vec2(-0.354, 0.189)));\n"
        "    d = min(d, jg_s(uv, vec2(0.152, -0.219), vec2(0.247138, -0.197006),vec2(0.203037, -0.049533)));\n"
        "    d = min(d, jg_s(uv, vec2(0.203037, -0.049533), vec2(0.158936, 0.121671),vec2(0.177594, 0.243718)));\n"
        "    d = min(d, jg_s(uv, vec2(0.177594, 0.243718), vec2(0.201, 0.318),vec2(0.267493, 0.279315)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.157333, -0.193419), vec2(-0.1908, -0.1535)));\n"
        "    return min(d, jg_l(uv, vec2(-0.157333, -0.193419), vec2(-0.340455, -0.174649)));\n"
        "}\n"
        "\n"
        "float jg_h43(vec2 uv) { // ro\n"
        "    float d = jg_s(uv, vec2(0.17433, -0.296286), vec2(-0.09899, -0.280485),vec2(-0.214191, -0.287257));\n"
        "    d = min(d, jg_s(uv, vec2(0.17433, -0.296286), vec2(-0.026707, -0.086351),vec2(-0.306803, 0.098753)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.306803, 0.098753), vec2(-0.159979, 0.008458),vec2(0.036541, -0.027659)));\n"
        "    d = min(d, jg_s(uv, vec2(0.036541, -0.027659), vec2(0.251131, -0.057005),vec2(0.29179, 0.085209)));\n"
        "    return min(d, jg_s(uv, vec2(0.29179, 0.085209), vec2(0.321155, 0.36738),vec2(-0.184826, 0.286115)));\n"
        "}\n"
        "\n"
        "float jg_h44(vec2 uv) { // wa\n"
        "    float d = jg_s(uv, vec2(0.041058, -0.163101), vec2(-0.159979, -0.097638),vec2(-0.345204, 0.180018));\n"
        "    d = min(d, jg_s(uv, vec2(0.041058, -0.163101), vec2(0.287272, -0.228565),vec2(0.318896, 0.012973)));\n"
        "    d = min(d, jg_s(uv, vec2(0.318896, 0.012973), vec2(0.343743, 0.23871),vec2(0.034282, 0.279343)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.19, 0.34), vec2(-0.18, -0.35)));\n"
        "    d = min(d, jg_l(uv, vec2(-0.342945, -0.176646), vec2(-0.144167, -0.196962)));\n"
        "    return min(d, jg_l(uv, vec2(-0.1833, -0.12), vec2(-0.144167, -0.196962)));\n"
        "}\n"
        "\n"
        "float jg_h45(vec2 uv) { // wo\n"
        "    float d = jg_s(uv, vec2(0.236574, -0.261088), vec2(-0.012916, -0.21905),vec2(-0.305921, -0.234996));\n"
        "    d = min(d, jg_s(uv, vec2(-0.034674, -0.348063), vec2(-0.095596, -0.169765),vec2(-0.317526, 0.073764)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.317526, 0.073764), vec2(0.136488, -0.303126),vec2(0.075566, 0.175234)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.098497, 0.296999), vec2(-0.326229, 0.127398),vec2(0.330858, -0.059597)));\n"
        "    return min(d, jg_s(uv, vec2(-0.098497, 0.296999), vec2(0.009, 0.362),vec2(0.288793, 0.305696)));\n"
        "}\n"
        "\n"
        "float jg_h46(vec2 uv) { // n\n"
        "    float d = jg_s(uv, vec2(-0.32093, 0.31), vec2(-0.171901, -0.1093),vec2(-0.083194, -0.33));\n"
        "    d = min(d, jg_s(uv, vec2(-0.32093, 0.31), vec2(-0.145289, -0.068),vec2(-0.002, -0.027)));\n"
        "    d = min(d, jg_s(uv, vec2(-0.002, -0.027), vec2(0.066, -0.011),vec2(0.060512, 0.18679)));\n"
        "    d = min(d, jg_s(uv, vec2(0.146704, 0.305019), vec2(0.300997, 0.314903),vec2(0.332647, 0.081638)));\n"
        "    return min(d, jg_s(uv, vec2(0.060512, 0.18679), vec2(0.0605, 0.303042),vec2(0.146704, 0.305019)));\n"
        "}\n"
        "\n"
        "float jg_hiragana(vec2 p, int c, float scale, int gs, float bevel, float rad)\n"
        "{\n"
        "    c = int(fract(float(gs > 1 ? c: c - 1)/46.0)*47.0)+1;\n"
        "\n"
        "    float s = 1.0/scale;\n"
        "    p *= s;\n"
        "\n"
        "    float d = 1.0;\n"
        "\n"
        "    if (c == 1) d = jg_h1(p);\n"
        "    if (c == 2) d = jg_h2(p);\n"
        "    if (c == 3) d = jg_h3(p);\n"
        "    if (c == 4) d = jg_h4(p);\n"
        "    if (c == 5) d = jg_h5(p);\n"
        "    if (c == 6) d = jg_h6(p);\n"
        "    if (c == 7) d = jg_h7(p);\n"
        "    if (c == 8) d = jg_h8(p);\n"
        "    if (c == 9) d = jg_h9(p);\n"
        "    if (c == 10) d = jg_h10(p);\n"
        "    if (c == 11) d = jg_h11(p);\n"
        "    if (c == 12) d = jg_h12(p);\n"
        "    if (c == 13) d = jg_h13(p);\n"
        "    if (c == 14) d = jg_h14(p);\n"
        "    if (c == 15) d = jg_h15(p);\n"
        "    if (c == 16) d = jg_h16(p);\n"
        "    if (c == 17) d = jg_h17(p);\n"
        "    if (c == 18) d = jg_h18(p);\n"
        "    if (c == 19) d = jg_h19(p);\n"
        "    if (c == 20) d = jg_h20(p);\n"
        "    if (c == 21) d = jg_h21(p);\n"
        "    if (c == 22) d = jg_h22(p);\n"
        "    if (c == 23) d = jg_h23(p);\n"
        "    if (c == 24) d = jg_h24(p);\n"
        "    if (c == 25) d = jg_h25(p);\n"
        "    if (c == 26) d = jg_h26(p);\n"
        "    if (c == 27) d = jg_h27(p);\n"
        "    if (c == 28) d = jg_h28(p);\n"
        "    if (c == 29) d = jg_h29(p);\n"
        "    if (c == 30) d = jg_h30(p);\n"
        "    if (c == 31) d = jg_h31(p);\n"
        "    if (c == 32) d = jg_h32(p);\n"
        "    if (c == 33) d = jg_h33(p);\n"
        "    if (c == 34) d = jg_h34(p);\n"
        "    if (c == 35) d = jg_h35(p);\n"
        "    if (c == 36) d = jg_h36(p);\n"
        "    if (c == 37) d = jg_h37(p);\n"
        "    if (c == 38) d = jg_h38(p);\n"
        "    if (c == 39) d = jg_h39(p);\n"
        "    if (c == 40) d = jg_h40(p);\n"
        "    if (c == 41) d = jg_h41(p);\n"
        "    if (c == 42) d = jg_h42(p);\n"
        "    if (c == 43) d = jg_h43(p);\n"
        "    if (c == 44) d = jg_h44(p);\n"
        "    if (c == 45) d = jg_h45(p);\n"
        "    if (c == 46) d = jg_h46(p);\n"
        "\n"
        "    return 1.0-clamp(0.0-(d-rad)/max(bevel, 0.00001), 0.0, 1.0);\n"
        "}\n"
        "\n"
        "float jg_c_h(vec2 p, int c, float scale, int gs, float bevel, float rad) {\n"
        "    if(gs > 1) {\n"
        "        if(jg_box(p,0.5) > 0.0) {\n"
        "            vec3 gr = jg_grid(p,gs);\n"
        "            return jg_hiragana(gr.xy,int(gr.z),scale,gs,bevel,rad);\n"
        "        }\n"
        "        return 1.0;\n"
        "    }\n"
        "    if (jg_box(p,0.5*scale) > 0.0)\n"
        "        return jg_hiragana(p,c,scale,gs,bevel,rad);\n"
        "    return 1.0;\n"
        "}\n"
        "\n"
        "float jg_katakana(vec2 p, int c, float scale, int gs, float bevel, float rad)\n"
        "{\n"
        "    c = int(fract(float(gs > 1 ? c: c - 1)/46.0)*47.0)+1;\n"
        "\n"
        "    float s = 1.0/scale;\n"
        "    p *= s;\n"
        "\n"
        "    float d = 1.0;\n"
        "\n"
        "    if (c == 1) d = jg_k1(p);\n"
        "    if (c == 2) d = jg_k2(p);\n"
        "    if (c == 3) d = jg_k3(p);\n"
        "    if (c == 4) d = jg_k4(p);\n"
        "    if (c == 5) d = jg_k5(p);\n"
        "    if (c == 6) d = jg_k6(p);\n"
        "    if (c == 7) d = jg_k7(p);\n"
        "    if (c == 8) d = jg_k8(p);\n"
        "    if (c == 9) d = jg_k9(p);\n"
        "    if (c == 10) d = jg_k10(p);\n"
        "    if (c == 11) d = jg_k11(p);\n"
        "    if (c == 12) d = jg_k12(p);\n"
        "    if (c == 13) d = jg_k13(p);\n"
        "    if (c == 14) d = jg_k14(p);\n"
        "    if (c == 15) d = jg_k15(p);\n"
        "    if (c == 16) d = jg_k16(p);\n"
        "    if (c == 17) d = jg_k17(p);\n"
        "    if (c == 18) d = jg_k18(p);\n"
        "    if (c == 19) d = jg_k19(p);\n"
        "    if (c == 20) d = jg_k20(p);\n"
        "    if (c == 21) d = jg_k21(p);\n"
        "    if (c == 22) d = jg_k22(p);\n"
        "    if (c == 23) d = jg_k23(p);\n"
        "    if (c == 24) d = jg_k24(p);\n"
        "    if (c == 25) d = jg_k25(p);\n"
        "    if (c == 26) d = jg_k26(p);\n"
        "    if (c == 27) d = jg_k27(p);\n"
        "    if (c == 28) d = jg_k28(p);\n"
        "    if (c == 29) d = jg_k29(p);\n"
        "    if (c == 30) d = jg_k30(p);\n"
        "    if (c == 31) d = jg_k31(p);\n"
        "    if (c == 32) d = jg_k32(p);\n"
        "    if (c == 33) d = jg_k33(p);\n"
        "    if (c == 34) d = jg_k34(p);\n"
        "    if (c == 35) d = jg_k35(p);\n"
        "    if (c == 36) d = jg_k36(p);\n"
        "    if (c == 37) d = jg_k37(p);\n"
        "    if (c == 38) d = jg_k38(p);\n"
        "    if (c == 39) d = jg_k39(p);\n"
        "    if (c == 40) d = jg_k40(p);\n"
        "    if (c == 41) d = jg_k41(p);\n"
        "    if (c == 42) d = jg_k42(p);\n"
        "    if (c == 43) d = jg_k43(p);\n"
        "    if (c == 44) d = jg_k44(p);\n"
        "    if (c == 45) d = jg_k45(p);\n"
        "    if (c == 46) d = jg_k46(p);\n"
        "\n"
        "    return 1.0-clamp(0.0-(d-rad)/max(bevel, 0.00001), 0.0, 1.0);\n"
        "}\n"
        "\n"
        "float jg_c_k(vec2 p, int c, float scale, int gs, float bevel, float rad) {\n"
        "    if(gs > 1) {\n"
        "        if(jg_box(p,0.5) > 0.0) {\n"
        "            vec3 gr = jg_grid(p,gs);\n"
        "            return jg_katakana(gr.xy,int(gr.z),scale,gs,bevel,rad);\n"
        "        }\n"
        "        return 1.0;\n"
        "    }\n"
        "    if (jg_box(p,0.5*scale) > 0.0)\n"
        "        return jg_katakana(p,c,scale,gs,bevel,rad);\n"
        "    return 1.0;\n"
        "}\n";
    add_input(d, "bevel_map", Value_type::grayscale, "1.0");
    add_enum(
        d, "sys", "System",
        {
            Enum_value{"Hiragana", "h"},
            Enum_value{"Katakana", "k"}
        },
        0
    );
    add_float(d, "char",  "Character", 1.0f,   1.0f, 46.0f, 1.0f);
    add_float(d, "scale", "Scale",     1.0f,   0.0f,  1.0f, 0.001f);
    add_float(d, "rad",   "Radius",    0.025f, 0.0f,  1.0f, 0.001f);
    add_float(d, "gs",    "Grid Size", 1.0f,   1.0f, 32.0f, 1.0f);
    add_float(d, "bevel", "Bevel",     0.01f,  0.0f,  1.0f, 0.001f);
    add_output(
        d, Value_type::grayscale,
        "1.0 - jg_c_$(sys)($uv-0.5, int($char), $scale, int($gs), $bevel*$bevel_map($uv), $rad)"
    );
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
        descriptors.push_back(build_voronoi_triangle());
        descriptors.push_back(build_wavelet_noise());
        descriptors.push_back(build_noise_anisotropic());
        descriptors.push_back(build_noise_white());
        descriptors.push_back(build_perlin_color());
        descriptors.push_back(build_shard_fbm());
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
        descriptors.push_back(build_pattern());
        descriptors.push_back(build_beehive());
        descriptors.push_back(build_cairo());
        descriptors.push_back(build_arc_pavement());
        descriptors.push_back(build_iching());
        descriptors.push_back(build_runes());
        descriptors.push_back(build_roman_numerals());
        descriptors.push_back(build_seven_segment());
        descriptors.push_back(build_scratches());
        descriptors.push_back(build_profile());
        descriptors.push_back(build_japanese_glyphs());
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
