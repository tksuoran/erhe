#pragma once

#include "assets/asset_key.hpp"

struct Asset_reference_data;

namespace editor {

// Conversions between the runtime Asset_key and its config codegen
// serialized form Asset_reference_data (asset-manager plan D2). Scope and
// type serialize as their c_str() names; an empty scope string round-trips
// to an empty key.
[[nodiscard]] auto to_asset_key           (const Asset_reference_data& data) -> Asset_key;
[[nodiscard]] auto to_asset_reference_data(const Asset_key& key) -> Asset_reference_data;

}
