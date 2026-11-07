#pragma once

#include <cstdint>
#include <optional>

namespace erhe::asset_store {

// Values are stored in the container: append new kinds, never renumber.
enum class Item_kind : std::uint32_t
{
    scene         = 1,
    local_mesh    = 2,
    world_mesh    = 3,
    raytrace_bvh  = 4,
    texture       = 5,
    basis_texture = 6,
    pvs           = 7,
    editor_data   = 8
};

[[nodiscard]] auto c_str(Item_kind kind) -> const char*;
[[nodiscard]] auto item_kind_from_value(std::uint32_t value) -> std::optional<Item_kind>;

}
