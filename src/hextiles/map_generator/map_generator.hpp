#pragma once

#include "map_generator/biome.hpp"
#include "map_generator/fbm_noise.hpp"
#include "map_generator/variations.hpp"

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "types.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include "etl/vector.h"

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace hextiles {

class Map;
class Map_editor;
class Tiles;

class Map_generator
    : public erhe::imgui::Imgui_window
{
public:
    Map_generator(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Map_editor&                  map_editor,
        Tiles&                       tiles
    );

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

    Map_editor& m_map_editor;
    Tiles&      m_tiles;

    Fbm_noise   m_noise;
    Variations  m_elevation_generator  {};
    Variations  m_temperature_generator{};
    Variations  m_humidity_generator   {};
    Variations  m_variation_generator  {};

    etl::vector<Biome, max_biome_count> m_biomes;
};

} // namespace hextiles
