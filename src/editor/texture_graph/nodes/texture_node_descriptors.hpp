#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace erhe::texgen {
    class Node_descriptor;
}

namespace editor {

// MVP texture node descriptors (Phase 3 Step 2 of doc/texture-graph-plan.md).
//
// Each descriptor is an immutable erhe::texgen::Node_descriptor whose GLSL is
// ported from Material Maker (https://github.com/RodZill4/material-maker, MIT
// license) - see the attribution comments in texture_node_descriptors.cpp for
// which node each snippet comes from and what was reduced. Descriptors are
// built once (function-local statics) and shared by every Texture_descriptor_node
// instance of the type.

// Returns the descriptor for the given factory type name (uniform, perlin,
// voronoi, bricks, shape, fbm, noise, color_noise, gradient, circular_gradient,
// radial_gradient, spiral_gradient, multigradient, sine_wave, truchet, weave,
// blend, colorize, curve, transform, brightness_contrast, normal_map, blur,
// math, invert, quantize, adjust_hsv, remap, combine, decompose, swap_channels,
// reroute, switch, switch_grayscale, switch_rgb, rotate, scale, shear, skew,
// mirror, repeat, swirl, spherize, magnify, kaleidoscope, warp,
// directional_warp, refract, uniform_greyscale, greyscale, tones, tones_map,
// tones_range, tones_step, tonality, convert_colorspace, colormap, palettize,
// default_color, compare, ensure_greyscale, ensure_rgba, pattern, beehive,
// cairo, arc_pavement, iching, runes, roman_numerals, seven_segment,
// scratches, profile, japanese_glyphs), or nullptr for an
// unknown name. The "output" / "material_output"
// sink nodes and the "buffer" node have no descriptor and are created directly
// by the factory.
[[nodiscard]] auto get_texture_node_descriptor(std::string_view type_name) -> const erhe::texgen::Node_descriptor*;

// All MVP descriptors, in toolbar order. Used by the factory and by the
// descriptor self-consistency / compose self-check.
[[nodiscard]] auto all_texture_node_descriptors() -> const std::vector<const erhe::texgen::Node_descriptor*>&;

// Composes every MVP descriptor standalone (all inputs unconnected, default
// parameters) and returns a human-readable message for each output whose
// assembled fragment contains a composition error marker. An empty result
// means every descriptor / output composed cleanly.
//
// This is the Step-2 stand-in for the compose smoke test: the editor has no
// gtest target, so descriptor <-> texgen consistency is checked here (run once
// at Texture_graph_window construction, results logged). The full graph-DAG
// compose plus GPU render is exercised by the Step 5/6 MCP smoke script.
[[nodiscard]] auto check_texture_node_descriptors() -> std::vector<std::string>;

} // namespace editor
