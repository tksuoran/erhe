#pragma once

#include "map_generator/biome.hpp"
#include "map_generator/fbm_noise.hpp"
#include "map_generator/variations.hpp"

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "types.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include "etl/vector.h"

namespace hextiles
{

class Map;

class Map_generator
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr const char* c_title{"Map Generator"};
    static constexpr const char* c_type_name{"Map_generator"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Map_generator();
    ~Map_generator();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
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

    Fbm_noise          m_noise;
    Variations         m_elevation_generator;
    Variations         m_temperature_generator;
    Variations         m_humidity_generator;
    Variations         m_variation_generator;

    etl::vector<Biome, max_biome_count> m_biomes;
};

extern Map_generator* g_map_generator;

} // namespace hextiles
