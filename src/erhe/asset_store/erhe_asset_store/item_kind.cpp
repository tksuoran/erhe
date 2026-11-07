#include "erhe_asset_store/item_kind.hpp"

namespace erhe::asset_store {

auto c_str(const Item_kind kind) -> const char*
{
    switch (kind) {
        case Item_kind::scene:         return "scene";
        case Item_kind::local_mesh:    return "local_mesh";
        case Item_kind::world_mesh:    return "world_mesh";
        case Item_kind::raytrace_bvh:  return "raytrace_bvh";
        case Item_kind::texture:       return "texture";
        case Item_kind::basis_texture: return "basis_texture";
        case Item_kind::pvs:           return "pvs";
        case Item_kind::editor_data:   return "editor_data";
        default:                       return "?";
    }
}

auto item_kind_from_value(const std::uint32_t value) -> std::optional<Item_kind>
{
    if ((value >= static_cast<std::uint32_t>(Item_kind::scene)) && (value <= static_cast<std::uint32_t>(Item_kind::editor_data))) {
        return static_cast<Item_kind>(value);
    }
    return {};
}

}
