#pragma once

#include "hextiles/coordinate.hpp"
//#include "hextiles/pixel_lookup.hpp"
#include "hextiles/terrain_type.hpp"
#include "hextiles/types.hpp"

#include "erhe/components/components.hpp"

#include <vector>
#include <memory>

namespace hextiles
{

class Tiles
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name {"Tiles"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Tiles ();
    ~Tiles() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void initialize_component() override;

    // Public API
    auto get_terrain_shape     (const uint16_t terrain) -> Pixel_coordinate;
    auto get_terrain_shapes    () const -> const std::vector<Pixel_coordinate>&;
    auto get_base_terrain      (terrain_t terrain) const -> terrain_t;
    //auto get_base_terrain      (Tile_coordinate tile_coordinate) const -> terrain_t;
    auto get_terrain_type      (terrain_t terrain) const -> const Terrain_type&;
    auto get_multishape_terrain(int group, unsigned int neighbor_mask) -> uint16_t;
    auto get_grid_shape        (int grid) const -> Pixel_coordinate;

private:
    void init_terrain_shapes();
    //void init_unit_shapes   ();
    void init_terrain_types ();
    void load_terrain_defs  ();
    //void load_unit_defs     ();

    //std::vector<UnitType> unit_types;
    //std::vector<Player> players;

    terrain_t                     m_multigroup_terrain_offset;
    uint16_t                      m_last_terrain;
    std::vector<Pixel_coordinate> m_terrain_shapes;
    //std::vector<Pixel_coordinate> m_unit_shapes;
    std::vector<Pixel_coordinate> m_extra_shapes; // border, brush sizes
    std::vector<Pixel_coordinate> m_grid_shapes;  // dotted grid, line grid, mask, target, shade
    std::vector<Pixel_coordinate> m_edge_shapes;  // N, NE, SE, S, SW, NW
    std::vector<Terrain_type>     m_terrain_types;
    //std::vector<Unit_type>        m_unit_types;
};

} // namespace hextiles
