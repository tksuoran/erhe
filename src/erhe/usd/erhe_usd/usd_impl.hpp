#pragma once

// Internal header: it includes LightUSD headers and is included only by
// erhe::usd's own translation units, never by a client of erhe::usd.

#include "erhe_usd/usd.hpp"

#include "stage.hh"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::usd {

// How a brush travels in a USD file (doc/usd-compatibility-plan.md E4a). USD
// has no schema for a brush, so the prim's `typeName` is the erhe class token
// - the same token the writer gives every `Typed` prim - its geometry is a
// child `Mesh` prim of a fixed name, and the two fields no erhe property
// carries are `erhe:Brush:` custom attributes named as the glTF fields are.
// The reader and the writer both spell them from here.
constexpr std::string_view c_brush_prim_type_name           {"Brush"};
constexpr std::string_view c_brush_geometry_prim_name       {"geometry"};
constexpr std::string_view c_brush_density_attribute        {"erhe:Brush:density"};
constexpr std::string_view c_brush_normal_style_attribute   {"erhe:Brush:normal_style"};
// The same two in the neutral `Owner.name` form read_spec_values hands back.
constexpr std::string_view c_brush_density_value_name       {"Brush.density"};
constexpr std::string_view c_brush_normal_style_value_name  {"Brush.normal_style"};

// How a texture node graph travels in a USD file
// (doc/usd-texture-graphs-plan.md 2.1). The graph is a `NodeGraph` prim
// carrying the marker attribute that says it is erhe's - a `NodeGraph`
// without it is a foreign shading network (R5) - each node is a `Shader`
// child whose `info:id` is the node's factory type name under a namespace
// prefix, and the node's editor position is one custom attribute. The reader
// and the writer both spell them from here.
constexpr std::string_view c_node_graph_prim_type_name     {"NodeGraph"};
constexpr std::string_view c_node_graph_format_attribute   {"erhe:graph:format"};
constexpr std::string_view c_node_graph_position_attribute {"erhe:ui:position"};
constexpr std::string_view c_node_graph_node_id_prefix     {"erhe:texture:"};
constexpr std::string_view c_node_graph_input_prefix       {"inputs:"};
constexpr std::string_view c_node_graph_output_prefix      {"outputs:"};
constexpr std::string_view c_node_graph_shader_prim_type_name{"Shader"};
constexpr std::string_view c_node_graph_info_id_attribute  {"info:id"};

// The USD schema token of a point instancer (doc/usd-compatibility-plan.md
// S1). The reader dispatches on it and the writer spells it, so both name it
// from here; the erhe class is erhe::scene::Point_instancer.
constexpr std::string_view c_point_instancer_prim_type_name {"PointInstancer"};

// The USD schema token of a skeleton (doc/usd-compatibility-plan.md K1). A
// `Skeleton` prim is a transformable prim of the erhe tree carrying this
// token, holding one `Xform` prim per joint; the reader dispatches on the
// token and the writer spells it, so both name it from here.
constexpr std::string_view c_skeleton_prim_type_name {"Skeleton"};

// One prim a variant block authors as a `def` child and the loader hoisted
// out of it, into the tree below the prim carrying the set
// (doc/usd-compatibility-plan.md X4). USD builds such a prim when its variant
// is selected and takes it
// away again on a switch; erhe keeps every variant's prims in the tree and
// flips `active`, so a switch is a property write like every other one.
class Variant_prim_record final
{
public:
    // Stage path of the prim carrying the variant set.
    std::string carrier_path;
    std::string set_name;
    std::string variant_name;
    // The name the hoisted prim has below the carrier, sibling-unique by the
    // M2 rule: two variants of one set are free to author the same name and
    // the tree is not.
    std::string prim_name;
    // The name the variant block gave it, which is what a save writes back.
    std::string authored_name;
};

class Stage::Impl final
{
public:
    lightusd::Stage       stage;
    std::filesystem::path source_path;
    // The layer `stage` was built from, kept by load_stage: the root layer of
    // `source_path` composed with its `subLayers`, with the prims of every
    // variant block hoisted into it - exactly the spec tree behind the prims
    // of `stage`. It is the source of everything LightUSD does not compose -
    // the `class` prims, the `over` opinions, the `variantSet` blocks - which
    // the importer reads back off it rather than re-reading the file for
    // itself, so a prim any layer of the stack authors contributes those the
    // way a root-layer prim does.
    lightusd::Layer       layer;
    bool                  layer_ok{false};
    // The prims the variant blocks of `layer` authored, in the tree of
    // `stage` since load_stage hoisted them there.
    std::vector<Variant_prim_record> variant_prims;
};

} // namespace erhe::usd
