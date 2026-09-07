#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe { class Hierarchy; class Item_base; }

namespace erhe::gltf {

// An object-reference local value of a "properties" map whose path did not
// resolve while the file was parsed: the item had no Item_host yet, or the
// referenced item (a content-library physics material) is only created by
// an operation the editor queues after the parse. The editor resolves the
// name in its scene once everything exists (Gltf_data::unresolved_object_properties).
class Unresolved_object_property
{
public:
    std::shared_ptr<erhe::Item_base> item;
    std::string                      property_name;
    std::string                      text;
};

// The persistent (authored) Item flags serialized by NAME in ERHE_*
// extensions (doc/gltf-scene-roundtrip-plan.md phase 3). Names, never raw
// bit values: bit positions are not stable across erhe versions; unknown
// names are ignored on load so the set can grow. Transient presentation
// state (selected, hovered_*, negative_determinant, affects_shadow) and the
// structurally handled import_root are deliberately absent from the set.
// Shared by the exporter-internal ERHE_node / ERHE_camera / ERHE_light
// writers and the editor-domain extension builders (ERHE_layout).

// JSON array of persistent flag names for the set bits, e.g.
// ["visible","content"].
[[nodiscard]] auto persistent_item_flags_to_json(uint64_t flag_bits) -> std::string;

// The flag bit for a serialized name; 0 when the name is unknown.
[[nodiscard]] auto persistent_item_flag_from_name(std::string_view name) -> uint64_t;

// Applies an accumulated set of listed flag bits exactly: every persistent
// flag is enabled when listed and disabled when not. Bits outside the
// persistent set and the derived bits (Item_flags::derived, see below) are
// left untouched.
void apply_persistent_item_flags(erhe::Item_base& item, uint64_t listed_bits);

// Local property values (doc/property-system.md D23 / D14): the
// "properties" object next to "flags" holds every local value of the item's
// registered properties that is stored (not bridged, not an expression)
// and flagged serialize, as name -> D16 text, e.g. {"visible":"false"}.
// The object is written even when empty: its presence marks a file whose
// visible / shadow_cast / lightmapped come from properties, not flags.
[[nodiscard]] auto item_local_properties_to_json(const erhe::Item_base& item) -> std::string;

// The sparse overrides the prefab instance under `carrier` holds
// (doc/usd-compatibility-plan.md X2, doc/gltf_extensions/ERHE_node.md), as
// the ERHE_node "overrides" array: one object per item that holds any, with
// its M1 path below the arc's target clone, its local values in the same
// name -> D16 text form "properties" uses, and its local transform as 16
// floats when it differs from the template counterpart's. The empty string
// when the instance holds none, so the member is written only where there is
// something to write.
[[nodiscard]] auto instance_overrides_to_json(const erhe::Hierarchy& carrier) -> std::string;

// Applies one serialized local value; false (logged) for an unknown name
// or a value that does not parse as the property's type.
auto apply_item_local_property(erhe::Item_base& item, std::string_view name, std::string_view value) -> bool;

// apply_item_local_property, except that an object reference whose path
// does not resolve is appended to `unresolved` (not logged) for the
// editor's late resolution; false for an unknown name or a value that
// does not parse.
auto apply_item_local_property(erhe::Item_base& item, std::string_view name, std::string_view value, std::vector<Unresolved_object_property>& unresolved) -> bool;

// The "properties" object is the item's complete local set: every stored
// (not bridged, not computed, not read-only) value property of the item's
// own chain that holds a local value `is_listed` does not accept is
// cleared, so a value the core glTF fields carried (a KHR_lights_punctual
// color, a KHR_physics_rigid_bodies friction) does not turn an inherited
// value into a local one on reload. Call after the listed values are
// applied.
void clear_local_properties_not_listed(erhe::Item_base& item, const std::function<bool(std::string_view name)>& is_listed);

// Older files list visible / shadow_cast / lightmapped in the flags
// arrays: a listed shadow_cast / lightmapped becomes a local true, an
// unlisted visible a local false. Call only when the payload has no
// "properties" object.
void apply_legacy_derived_item_flags(erhe::Item_base& item, uint64_t listed_bits);

}
