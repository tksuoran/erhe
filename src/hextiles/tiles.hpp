#pragma once

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "unit_type.hpp"
#include "types.hpp"

#include "erhe/components/components.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace hextiles
{

class Tiles
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Tiles"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Tiles ();
    ~Tiles() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    auto get_terrain_from_tile             (terrain_tile_t terrain_tile) const -> terrain_t;
    auto get_terrain_tile_from_terrain     (terrain_t terrain) const -> terrain_tile_t;
    auto get_terrain_type                  (terrain_t terrain) const -> const Terrain_type&;
    auto get_terrain_type                  (terrain_t terrain) -> Terrain_type&;
    auto get_terrain_group_tile            (int group, unsigned int neighbor_mask) const -> terrain_tile_t;
    auto get_terrain_type_count            () const -> size_t;
    auto get_terrain_group_count           () const -> size_t;
    auto get_terrain_replacement_rule_count() const -> size_t;
    auto get_unit_type_count               () const -> size_t;
    auto get_unit_type                     (unit_t unit) const -> const Unit_type&;
    auto get_unit_type                     (unit_t unit) -> Unit_type&;
    auto get_city_unit_type                (int city_size) const -> unit_t;
    auto get_terrain_group                 (size_t group) const -> const Terrain_group&;
    auto get_terrain_group                 (size_t group) -> Terrain_group&;
    auto get_terrain_replacement_rule      (size_t replacement_rule) const -> const Terrain_replacement_rule&;
    auto get_terrain_replacement_rule      (size_t replacement_rule) -> Terrain_replacement_rule&;

    void update_terrain_groups();

    auto remove_terrain_group           (int group_id) -> bool;
    auto remove_terrain_replacement_rule(int rule_id) -> bool;
    void add_terrain_group              ();
    void add_terrain_replacement_rule   ();

    void load_terrain_defs   ();
    void save_terrain_defs   ();
    void load_unit_defs      ();
    void save_unit_defs      ();

    void load_terrain_group_defs();
    void save_terrain_group_defs();

    void load_terrain_replacement_rule_defs();
    void save_terrain_replacement_rule_defs();

private:
    void init_shapes         ();
    //void init_terrain_types  ();

    void load_terrain_defs_v4();
    void load_terrain_defs_v5();
    void save_terrain_defs_v5();

    void load_terrain_group_defs_v1();
    void save_terrain_group_defs_v1();

    void load_unit_defs_v1();
    void load_unit_defs_v2();
    void save_unit_defs_v2();

    void load_terrain_replacement_rule_defs_v1();
    void save_terrain_replacement_rule_defs_v1();

    std::vector<Terrain_type>             m_terrain_types;
    std::vector<Terrain_group>            m_terrain_groups;
    std::vector<Terrain_replacement_rule> m_terrain_replacement_rules;
    std::vector<Unit_type>                m_unit_types;
};

} // namespace hextiles
