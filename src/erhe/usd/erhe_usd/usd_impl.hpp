#pragma once

// Internal header: it includes LightUSD headers and is included only by
// erhe::usd's own translation units, never by a client of erhe::usd.

#include "erhe_usd/usd.hpp"

#include "stage.hh"

#include <filesystem>
#include <string_view>

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

class Stage::Impl final
{
public:
    lightusd::Stage       stage;
    std::filesystem::path source_path;
};

} // namespace erhe::usd
