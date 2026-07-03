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

// Registry: name -> descriptor, built once. The vector<const Node_descriptor*>
// keeps toolbar order and drives the compose self-check.
struct Descriptor_registry
{
    std::array<Node_descriptor, 11> descriptors;
    std::vector<const Node_descriptor*> ordered;

    Descriptor_registry()
    {
        descriptors[0]  = build_uniform();
        descriptors[1]  = build_perlin();
        descriptors[2]  = build_voronoi();
        descriptors[3]  = build_bricks();
        descriptors[4]  = build_shape();
        descriptors[5]  = build_blend();
        descriptors[6]  = build_colorize();
        descriptors[7]  = build_curve();
        descriptors[8]  = build_transform();
        descriptors[9]  = build_brightness_contrast();
        descriptors[10] = build_normal_map();
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
