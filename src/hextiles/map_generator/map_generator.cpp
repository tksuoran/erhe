#include "map_generator/map_generator.hpp"
#include "log.hpp"
#include "map.hpp"
#include "map_editor/map_editor.hpp"
#include "tiles.hpp"

#include "erhe/application/imgui_windows.hpp"

#include <imgui.h>

namespace hextiles
{

Map_generator::Map_generator()
    : Component   {c_label}
    , Imgui_window{c_title, c_label}
{
}

Map_generator::~Map_generator()
{
}

void Map_generator::connect()
{
    m_tiles = require<Tiles>();
    require<erhe::application::Imgui_windows>();
}

void Map_generator::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Map_generator::update_elevation_terrains()
{
    const terrain_t terrain_count = static_cast<terrain_t>(m_tiles->get_terrain_type_count());

    std::vector<Terrain_variation> new_elevation_terrains;
    std::vector<Terrain_variation> new_variation_terrains;
    int min_temperature = std::numeric_limits<int>::max();
    int max_temperature = std::numeric_limits<int>::lowest();
    int min_humidity    = std::numeric_limits<int>::max();
    int max_humidity    = std::numeric_limits<int>::lowest();
    for (terrain_t t = 0; t < terrain_count; ++t)
    {
        const Terrain_type terrain = m_tiles->get_terrain_type(t);

        min_temperature = std::min(terrain.generate_min_temperature, min_temperature);
        max_temperature = std::max(terrain.generate_max_temperature, max_temperature);
        min_humidity    = std::min(terrain.generate_min_humidity,    min_humidity);
        max_humidity    = std::max(terrain.generate_max_humidity,    max_humidity);
    }
    const float temperature_extent = static_cast<float>(max_temperature - min_temperature);
    const float humidity_extent    = static_cast<float>(max_humidity    - min_humidity);
    log_map_generator.trace(
        "temperature: min = {}, max = {}, extent = {}\n",
        min_temperature,
        max_temperature,
        temperature_extent
    );
    log_map_generator.trace(
        "humidity: min = {}, max = {}, m_humidity_extent = {}\n",
        min_humidity,
        max_humidity,
        humidity_extent
    );
    m_biomes.clear();

    for (terrain_t t = 0; t < terrain_count; ++t)
    {
        const Terrain_type terrain = m_tiles->get_terrain_type(t);

        if (terrain.generate_elevation != 0)
        {
            new_elevation_terrains.push_back(
                m_elevation_generator.make(
                    terrain.generate_elevation,
                    t,
                    terrain.generate_ratio
                )
            );
        }
        else if (terrain.generate_base != 0)
        {
            if (
                (terrain.generate_min_temperature != 0) ||
                (terrain.generate_max_temperature != 0) ||
                (terrain.generate_min_humidity    != 0) ||
                (terrain.generate_max_humidity    != 0)
            )
            {
                m_biomes.push_back(
                    Biome
                    {
                        .base_terrain    = terrain.generate_base,
                        .variation       = t,
                        .priority        = terrain.generate_priority,
                        .min_temperature = static_cast<float>(terrain.generate_min_temperature - min_temperature) / temperature_extent,
                        .max_temperature = static_cast<float>(terrain.generate_max_temperature - min_temperature) / temperature_extent,
                        .min_humidity    = static_cast<float>(terrain.generate_min_humidity    - min_humidity   ) / humidity_extent,
                        .max_humidity    = static_cast<float>(terrain.generate_max_humidity    - min_humidity   ) / humidity_extent,
                    }
                );
            }
            else
            {
                const int id = static_cast<int>(new_variation_terrains.size());
                new_variation_terrains.push_back(
                    m_variation_generator.make(
                        id,
                        terrain.generate_base,
                        t
                    )
                );
            }
        }
    }

    m_elevation_generator.assign(std::move(new_elevation_terrains));
    m_variation_generator.assign(std::move(new_variation_terrains));

    //for (const auto& entry : m_elevation_generator.m_terrains)
    //{
    //    const Terrain_type terrain = m_tiles->get_terrain_type(entry.base_terrain);
    //    log_map_generator.trace(
    //        "elevation: base terrain {} - {}, elevation = {}, ratio = {}\n",
    //        entry.base_terrain,
    //        terrain.name,
    //        entry.id,
    //        entry.ratio
    //    );
    //}

    std::sort(
        m_biomes.begin(),
        m_biomes.end(),
        [](const Biome& lhs, const Biome& rhs)
        {
            // Sort first by priority
            if (lhs.priority != rhs.priority)
            {
                return lhs.priority > rhs.priority;
            }

            // then by average temperature
            const auto lhs_temperature = lhs.min_temperature + lhs.max_temperature;
            const auto rhs_temperature = rhs.min_temperature + rhs.max_temperature;
            if (lhs_temperature != rhs_temperature)
            {
                return lhs_temperature < rhs_temperature;
            }

            // then by average humidity
            const auto lhs_humidity = lhs.min_humidity + lhs.max_humidity;
            const auto rhs_humidity = rhs.min_humidity + rhs.max_humidity;
            if (lhs_humidity != rhs_humidity)
            {
                return lhs_humidity < rhs_humidity;
            }
            return false;
        }
    );

    //for (const Biome& biome : m_biomes)
    //{
    //    const Terrain_type base_terrain = m_tiles->get_terrain_type(biome.base_terrain);
    //    const Terrain_type variation    = m_tiles->get_terrain_type(biome.variation);
    //    log_map_generator.trace(
    //        "biome: base terrain {}, variation {}, temperature = {}..{}, humidity = {}..{}\n",
    //        base_terrain.name,
    //        variation.name,
    //        biome.min_temperature,
    //        biome.max_temperature,
    //        biome.min_humidity,
    //        biome.max_humidity
    //    );
    //}
}

void Map_generator::generate_noise_pass(Map& map)
{
    // In the first pass, we just generate noise values
    const int    width  = map.width();
    const int    height = map.height();
    const size_t count  = static_cast<size_t>(width) * static_cast<size_t>(height);

    update_elevation_terrains();

    m_elevation_generator  .reset(count);
    m_temperature_generator.reset(count);
    m_humidity_generator   .reset(count);
    m_variation_generator  .reset(count);
    const glm::vec4 elevation_seed  {12334.1f, 14378.0f, 12381.1f, 14386.9f};
    const glm::vec4 temperature_seed{27865.9f, 24387.6f, 28726.5f, 28271.4f};
    const glm::vec4 humidity_seed   {38760.8f, 39732.0f, 39785.6f, 32317.8f};
    const glm::vec4 variation_seed  {41902.6f, 41986.3f, 42098.7f, 43260.9f};
    for (coordinate_t tx = 0; tx < width; ++tx)
    {
        const float x        = static_cast<float>(tx) / static_cast<float>(width);
        const float y_offset = (tx & 1) == 1 ? -0.5f : 0.0f;
        for (coordinate_t ty = 0; ty < height; ++ty)
        {
            const float y = (static_cast<float>(ty) + y_offset) / static_cast<float>(height);
            const float elevation   = m_noise.generate(x, y, elevation_seed  );
            const float temperature = m_noise.generate(x, y, temperature_seed);
            const float humidity    = m_noise.generate(x, y, humidity_seed   );
            const float variation   = m_noise.generate(x, y, variation_seed  );
            m_elevation_generator  .push(elevation  );
            m_temperature_generator.push(temperature);
            m_humidity_generator   .push(humidity   );
            m_variation_generator  .push(variation  );
        }
    }
}

void Map_generator::generate_base_terrain_pass(Map& map)
{
    // Second pass converts noise values to terrain values based on thresholds
    m_elevation_generator.compute_threshold_values();

    //for (const auto& entry : m_elevation_generator.m_terrains)
    //{
    //    const Terrain_type& terrain = m_tiles->get_terrain_type(entry.base_terrain);
    //    log_map_window.trace(
    //        "terrain {} - {}, elevation = {}, ratio = {}, normalized ratio = {}, threshold = {}\n",
    //        entry.base_terrain,
    //        terrain.name,
    //        entry.id,
    //        entry.ratio,
    //        entry.normalized_ratio,
    //        entry.threshold
    //    );
    //}

    const int w = map.width();
    const int h = map.height();

    size_t index = 0;
    for (coordinate_t tx = 0; tx < w; ++tx)
    {
        for (coordinate_t ty = 0; ty < h; ++ty)
        {
            const Terrain_variation terrain_variation = m_elevation_generator.get(index);
            const terrain_tile_t    terrain_tile      = m_tiles->get_terrain_tile_from_terrain(terrain_variation.base_terrain);
            map.set_terrain_tile(Tile_coordinate{tx, ty}, terrain_tile);
            ++index;
        }
    }
}

auto Map_generator::get_variation(
    terrain_t base_terrain,
    float     temperature,
    float     humidity
) const -> terrain_t
{
    for (const Biome& biome : m_biomes)
    {
        if (base_terrain != biome.base_terrain)
        {
            continue;
        }
        if (temperature < biome.min_temperature)
        {
            continue;
        }
        if (humidity < biome.min_humidity)
        {
            continue;
        }
        if (temperature > biome.max_temperature)
        {
            continue;
        }
        if (humidity > biome.max_humidity)
        {
            continue;
        }
        return biome.variation;
    }
    return base_terrain;
}

void Map_generator::generate_variation_pass(Map& map)
{
    m_temperature_generator.compute_threshold_values();
    m_humidity_generator   .compute_threshold_values();
    m_variation_generator  .compute_threshold_values();

    const int width  = map.width();
    const int height = map.height();

    size_t index = 0;
    for (coordinate_t tx = 0; tx < width; ++tx)
    {
        for (coordinate_t ty = 0; ty < height; ++ty)
        {
            const Tile_coordinate position{tx, ty};
            const terrain_tile_t  terrain_tile   = map.get_terrain_tile(position);
            const terrain_t       terrain        = m_tiles->get_terrain_from_tile(terrain_tile);
            const float           temperature    = m_temperature_generator.get_noise_value(index);
            const float           humidity       = m_humidity_generator   .get_noise_value(index);
            //const float           variation    = m_variation_generator  .get_noise_value(index);
            const terrain_t       v_terrain      = get_variation(terrain, temperature, humidity);
            const terrain_tile_t  v_terrain_tile = m_tiles->get_terrain_tile_from_terrain(v_terrain);
            map.set_terrain_tile(position, v_terrain_tile);
            ++index;
        }
    }
}

void Map_generator::apply_rule(
    Map&                            map,
    const Terrain_replacement_rule& rule
)
{
    std::function<void(Tile_coordinate)> post_process_op =
    [this, &rule, &map](Tile_coordinate tile_position) -> void
    {
        const terrain_tile_t primary_terrain_tile = map.get_terrain_tile(tile_position);
        const terrain_t      primary_terrain      = m_tiles->get_terrain_from_tile(primary_terrain_tile);
        if (primary_terrain != rule.primary)
        {
            return;
        }
        std::function<void(Tile_coordinate)> replace =
        [this, &rule, &map] (Tile_coordinate position) -> void
        {
            const terrain_tile_t secondary_terrain_tile = map.get_terrain_tile(position);
            const terrain_t      secondary_terrain      = m_tiles->get_terrain_from_tile(secondary_terrain_tile);
            const bool found = std::find(
                rule.secondary.begin(),
                rule.secondary.end(),
                secondary_terrain
            ) != rule.secondary.end();
            const bool apply = rule.equal ? found : !found;

            if (apply)
            {
                const terrain_tile_t replacement_terrain_tile = m_tiles->get_terrain_tile_from_terrain(rule.replacement);
                map.set_terrain_tile(position, replacement_terrain_tile);
            }
        };
        map.hex_circle(tile_position, 0, 1, replace);
    };
    map.for_each_tile(post_process_op);
}

void Map_generator::generate_apply_rules_pass(Map& map)
{
    // Third pass does post-processing, adjusting neighoring
    // tiles based on a few rules.

    const size_t rule_count = m_tiles->get_terrain_replacement_rule_count();
    for (size_t i = 0; i < rule_count; ++i)
    {
        const Terrain_replacement_rule rule = m_tiles->get_terrain_replacement_rule(i);
        if (!rule.enabled)
        {
            continue;
        }
        apply_rule(map, rule);
    }
}

void Map_generator::generate_group_fix_pass(Map& map)
{
    // Apply terrain group rules
    const Tiles& tiles = *m_tiles.get();

    map.for_each_tile(
        [this, &map, &tiles](Tile_coordinate tile_position)
        {
            map.update_group_terrain(tiles, tile_position);
        }
    );
    map.for_each_tile(
        [this, &map, &tiles](Tile_coordinate tile_position)
        {
            map.update_group_terrain(tiles, tile_position);
        }
    );
}

void Map_generator::imgui()
{
    constexpr ImVec2 button_size{100.0f, 0.0f};

    if (
        !ImGui::TreeNodeEx(
            "Generate",
            ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen
        )
    )
    {
        return;
    }

    if (ImGui::TreeNodeEx("Elevation", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    {
        int slot = 0;
        for (Terrain_variation& elevation_terrain : m_elevation_generator.m_terrains)
        {
            Terrain_type& terrain_type = m_tiles->get_terrain_type(elevation_terrain.base_terrain);

            const auto label = fmt::format("{}##elevation-{}", terrain_type.name, ++slot);
            ImGui::SliderFloat(
                label.c_str(),
                &terrain_type.generate_ratio,
                0.0f,
                10.0f
            );
        }
        ImGui::TreePop();
    }
    update_elevation_terrains();

    if (ImGui::TreeNodeEx("Noise", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    {
        m_noise.imgui();
        ImGui::TreePop();
    }

    if (ImGui::Button("Generate", button_size))
    {
        m_noise.prepare();

        Map& map = *get<Map_editor>()->get_map().get();
        generate_noise_pass       (map);
        generate_base_terrain_pass(map);
        generate_apply_rules_pass (map);
        generate_group_fix_pass   (map);
        generate_variation_pass   (map);
        generate_group_fix_pass   (map);
    }

    ImGui::TreePop();
}


} // namespace hextiles
