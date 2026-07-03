#pragma once

#include "erhe_texgen/value_type.hpp"

#include <cstddef>

namespace editor {

class Texture_graph_node;

// Pin keys used by the texture graph. erhe::graph::Graph::connect() rejects
// links between pins whose keys differ, so giving each Value_type its own key
// makes connections type safe (grayscale pins only connect to grayscale pins,
// and so on). This mirrors Material Maker's slot classes; the f / rgb / rgba
// conversions between them are applied later at composition time
// (erhe::texgen::convert), not by the pin-key check.
class Texture_pin_key
{
public:
    static constexpr std::size_t grayscale = 1; // erhe::texgen::Value_type::grayscale
    static constexpr std::size_t rgb       = 2; // erhe::texgen::Value_type::rgb
    static constexpr std::size_t rgba      = 3; // erhe::texgen::Value_type::rgba
};

// Maps a texgen Value_type to its Texture_pin_key.
[[nodiscard]] auto value_type_to_pin_key(erhe::texgen::Value_type type) -> std::size_t;

// Value carried through texture graph pins.
//
// Unlike the geometry graph - whose payload holds a concrete result
// (geometry, scalar, ...) - a texture graph link carries a *reference to the
// producing node output*, not a texture. Rendering happens only at sinks
// (preview thumbnails, the output node), where the sink walks upstream via
// these references and builds an erhe::texgen::Compose_node DAG mirroring the
// editor graph, then composes and compiles one fragment shader (Material
// Maker's GLSL composition model, doc/texture-graph-plan.md decision 1).
//
// source_node points at the live node that produced this output. Because
// texture evaluation is synchronous on the live graph (decision 8, no async
// shadow clone), the pointer is valid for the duration of one evaluation
// pass: nodes are visited in topological order, so when a sink reads its
// inputs every upstream node has already run and set its output payloads.
// Structural edits mark sinks dirty and re-run evaluation before any sink
// reads a payload again, so a deleted node's pointer is never dereferenced.
class Texture_payload
{
public:
    [[nodiscard]] auto has_value() const -> bool;

    Texture_graph_node*      source_node {nullptr};
    std::size_t              output_index{0};
    erhe::texgen::Value_type value_type  {erhe::texgen::Value_type::rgba};
};

} // namespace editor
