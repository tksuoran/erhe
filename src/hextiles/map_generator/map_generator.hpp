#pragma once

#include "map_generator/biome.hpp"
#include "map_generator/fbm_noise.hpp"
#include "map_generator/variations.hpp"

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "types.hpp"

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <vector>

namespace hextiles
{

class Map;
class Map_editor;
class Tiles;

class Map_generator
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Map_generator"};
    static constexpr std::string_view c_title{"Map Tool"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Map_generator ();
    ~Map_generator() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

private:
    void update_elevation_terrains ();
    void generate_noise_pass       (Map& map);
    void generate_base_terrain_pass(Map& map);
    auto get_variation             (terrain_t base_terrain, float temperature, float humidity) const -> terrain_t;
    void generate_variation_pass   (Map& map);
    void apply_rule                (Map& map, const Terrain_replacement_rule& rule);
    void generate_apply_rules_pass (Map& map);
    void generate_group_fix_pass   (Map& map);

    // Component dependencies
    std::shared_ptr<Tiles> m_tiles;

    Fbm_noise          m_noise;

    Variations         m_elevation_generator;
    Variations         m_temperature_generator;
    Variations         m_humidity_generator;
    Variations         m_variation_generator;

    std::vector<Biome> m_biomes;
};

} // namespace hextiles
