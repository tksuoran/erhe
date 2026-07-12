#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace erhe { class Item_base; }

namespace erhe::gltf {

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
// persistent set are left untouched.
void apply_persistent_item_flags(erhe::Item_base& item, uint64_t listed_bits);

}
