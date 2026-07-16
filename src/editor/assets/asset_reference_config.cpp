#include "assets/asset_reference_config.hpp"

#include "config/generated/asset_reference_data.hpp"

namespace editor {

auto to_asset_key(const Asset_reference_data& data) -> Asset_key
{
    // An empty scope string means no reference; parse_asset_scope maps it
    // (and anything unknown) to Asset_scope::none, so the key stays empty.
    Asset_key key;
    key.scope = parse_asset_scope(data.scope);
    key.type  = parse_asset_type(data.asset_type);
    key.path  = data.path;
    key.uid   = data.uid;
    key.name  = data.name;
    if (key.scope == Asset_scope::none) {
        return Asset_key{};
    }
    return key;
}

auto to_asset_reference_data(const Asset_key& key) -> Asset_reference_data
{
    Asset_reference_data data;
    if (key.scope == Asset_scope::none) {
        return data; // all-default: omit_defaults drops it from the config
    }
    data.scope      = c_str(key.scope);
    data.asset_type = c_str(key.type);
    data.path       = key.path;
    data.uid        = key.uid;
    data.name       = key.name;
    return data;
}

}
