#pragma once

#include "types.hpp"

#include "erhe/components/components.hpp"
#include "erhe/scene/viewport.hpp"

namespace hextiles
{

class Rendering
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Rendering"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Rendering();
    ~Rendering();

    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void initialize_component() override;

    auto terrain_image          (terrain_tile_t terrain_tile, const int scale) -> bool;
    auto unit_image             (unit_tile_t unit_tile, const int scale) -> bool;
    void show_texture           ();
    void make_terrain_type_combo(const char* label, terrain_t& value);
    void make_unit_type_combo   (const char* label, unit_t& value, int player = 0);
};

extern Rendering* g_rendering;

}
