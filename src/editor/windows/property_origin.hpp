#pragma once

#include <string>

namespace erhe           { class Item_base; }
namespace erhe::property { class Dependency_property; }

namespace editor {

class App_context;

// The composition arc that brings a value to the prim it is read on
// (doc/usd-compatibility-plan.md X5).
enum class Property_arc : unsigned int {
    none       = 0, // no layer authors the value: a schema fallback, a computed value
    root_layer = 1, // authored on the prim itself, in the scene's own file
    reference  = 2, // a `references` arc
    payload    = 3, // a `payload` arc
    inherits   = 4  // an `inherits` arc: a class prim, an erhe style (X3)
};

[[nodiscard]] auto c_str(Property_arc arc) -> const char*;

// Where a property value comes from, in the terms the scene's own file format
// uses. erhe resolves every composition arc itself - references and payloads
// as prefab instances with the reference layer (X1, X2), `over` opinions as
// local values (X2), class inherits as styles (X3) - so the erhe value source
// IS the composition provenance: this is that source restated as the file's
// own structure, derived on demand and stored nowhere.
//
// - `layer`     the file the value is authored in ("session" for a scene that
//               has no file yet), empty when no layer authors it;
// - `prim_path` the absolute prim path in THAT layer which authors it;
// - `arc`       the arc that brings it to the item's own prim;
// - `arc_target` the arc's target: `<file></prim>` for a reference or a
//               payload, the class prim path for an inherits, empty otherwise;
// - `authored_as` the attribute the file's writer spells the value as.
class Property_origin final
{
public:
    std::string  layer      {};
    std::string  prim_path  {};
    Property_arc arc        {Property_arc::none};
    std::string  arc_target {};
    std::string  authored_as{};
};

// The origin of `property` as read on `item`. Cold path: it walks the item's
// ancestors and formats strings, so it is called for a row being hovered or
// for an MCP reply, never for every row of every frame.
[[nodiscard]] auto describe_property_origin(
    App_context&                               context,
    const erhe::Item_base&                     item,
    const erhe::property::Dependency_property& property
) -> Property_origin;

// The origin as the Properties window's tooltip shows it: one line per field
// the origin has, each beginning with a newline so the caller appends it to
// the tooltip it has built.
[[nodiscard]] auto property_origin_tooltip(const Property_origin& origin) -> std::string;

}
