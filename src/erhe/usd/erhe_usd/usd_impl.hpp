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
    // The root layer of `source_path`, read once by load_stage: the source of
    // everything LightUSD does not compose - the `class` prims, the `over`
    // opinions, the `variantSet` blocks - which the importer reads back off
    // it rather than re-reading the file for itself.
    lightusd::Layer       root_layer;
    bool                  root_layer_ok{false};
    // The prims the variant blocks of `root_layer` authored, in the tree of
    // `stage` since load_stage hoisted them there.
    std::vector<Variant_prim_record> variant_prims;
};

} // namespace erhe::usd
