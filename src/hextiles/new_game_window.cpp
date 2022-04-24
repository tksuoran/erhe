#include "menu_window.hpp"
#include "game.hpp"
#include "game_window.hpp"
#include "new_game_window.hpp"
#include "log.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "tiles.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

New_game_window::New_game_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
{
}

New_game_window::~New_game_window()
{
}

void New_game_window::connect()
{
    require<erhe::application::Imgui_windows>();
}

void New_game_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    m_players.clear();
    m_players.push_back("Player 1");
    m_players.push_back("Player 2");
}

namespace
{

auto random_coordinate(coordinate_t w, coordinate_t h) -> Tile_coordinate
{
    const coordinate_t x = rand() % w;
    const coordinate_t y = rand() % h;
    return Tile_coordinate{x, y};
}

}

auto New_game_window::is_too_close_to_city(Tile_coordinate location) const -> bool
{
    auto& map= get<Map_window>()->get_map();
    for (auto& city : m_cities)
    {
        auto distance = map->distance(city, location);
        if (distance < m_create_parameters.minimum_city_distance)
        {
            return true;
        }
    }
    return false;
}

auto New_game_window::try_create_city(uint32_t flags_match) -> bool
{
    auto& map   = get<Map_window>()->get_map();
    auto  tiles = get<Tiles>();

    const auto w = static_cast<coordinate_t>(map->width());
    const auto h = static_cast<coordinate_t>(map->height());

    const Tile_coordinate position   = random_coordinate(w, h);
    const terrain_t       terrain_id = map->get_terrain(position);
    const Terrain_type    terrain    = tiles->get_terrain_type(terrain_id);

    //if (terrain.city_size == terrain_city_size_match)
    //{
    //    log_new_game.info("{}, {} {}: {}\n", position.x, position.y, terrain.name, terrain.city_size);
    //}

    if ((terrain.flags & flags_match) != flags_match)
    {
        return false;
    }

    if (is_too_close_to_city(position))
    {
        return false;
    }

    m_cities.push_back(position);
    return true;
}

void New_game_window::select_player_start_cities()
{
    auto& map = get<Map_window>()->get_map();

    std::vector<size_t> player_start_cities;
    int                 minimum_distance = 99999999;
    for (size_t player = 0; player < m_players.size(); ++player)
    {
        size_t city_id = rand() % m_cities.size();
        player_start_cities.push_back(city_id);
    }
    for (size_t player_a = 0; player_a < m_players.size() - 1; ++player_a)
    {
        const auto& player_a_city = m_cities[player_start_cities[player_a]];
        for (size_t player_b = player_a + 1; player_b < m_players.size(); ++player_b)
        {
            const auto& player_b_city = m_cities[player_start_cities[player_b]];
            int distance = map->distance(player_a_city, player_b_city);
            minimum_distance = std::min(minimum_distance, distance);
        }
    }
    if (minimum_distance > m_minimum_player_start_city_distance)
    {
        m_minimum_player_start_city_distance = minimum_distance;
        m_player_start_cities = player_start_cities;
    }
}

void New_game_window::create()
{
    auto  game  = get<Game      >();
    auto& map   = get<Map_window>()->get_map();
    auto  tiles = get<Tiles>();

    place_cities();

    game->new_game();

    for (size_t i = 0; i < m_start_cities.size(); ++i)
    {
        const size_t          city_index   = m_start_cities[i];
        const Tile_coordinate location     = m_cities[city_index];
        const terrain_t       terrain_id   = map->get_terrain(location);
        const Terrain_type&   terrain_type = tiles->get_terrain_type(terrain_id);
        const int             city_size    = terrain_type.city_size;
        const unit_t          city_unit    = tiles->get_city_unit_type(city_size);

        map->set_unit_icon(location, city_unit);
    }
}

void New_game_window::place_cities()
{
    auto& map   = get<Map_window>()->get_map();
    auto  tiles = get<Tiles>();

    std::vector<terrain_t> city_tiles;

    const auto end = tiles->get_terrain_type_count();
    for (terrain_t i = 0; i < end; ++i)
    {
        const auto& terrain = tiles->get_terrain_type(i);
        if (terrain.city_size > 0)
        {
            city_tiles.push_back(i);
        }
    }

    for (auto& city_location : m_cities)
    {
        int city_tile = rand() % city_tiles.size();
        map->set_terrain(city_location, city_tiles.at(city_tile));

        const Terrain_type& city_terrain_type = tiles->get_terrain_type(
            map->get_terrain(city_location)
        );

        int field_count = city_terrain_type.city_size;
        map->hex_circle(
            city_location,
            0,
            4,
            [&map, &tiles, &field_count](const Tile_coordinate position)
            {
                if (field_count == 0)
                {
                    return;
                }

                const auto          t            = map->get_terrain(position);
                const Terrain_type& terrain_type = tiles->get_terrain_type(t);
                if ((terrain_type.flags & Terrain_flags::bit_can_place_field) == Terrain_flags::bit_can_place_field)
                {
                    map->set_terrain(position, Terrain_field);
                    --field_count;
                }
            }
        );
    }
}

void New_game_window::imgui()
{
    int player_number = 1;
    int player_to_remove = 0;
    for (auto& player : m_players)
    {
        auto label = fmt::format("Name of player {}", player_number);
        ImGui::InputText(label.c_str(), &player);
        ImGui::SameLine();
        auto remove_button_label = fmt::format("Remove##remove-player-{}", player_number);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            player_to_remove = player_number;
        }
        ++player_number;
    }
    if (player_to_remove > 0)
    {
        m_players.erase(m_players.begin() + player_to_remove - 1);
    }
    if (ImGui::Button("Add Player"))
    {
        m_players.push_back("Mr. Smith");
    }

    constexpr float drag_speed = 0.2f;

    ImGui::DragInt("Number of Coastal Cities",  &m_create_parameters.number_of_coastal_cities,  drag_speed, 1);
    ImGui::DragInt("Number of Land Cities",     &m_create_parameters.number_of_land_cities,     drag_speed, 1);
    ImGui::DragInt("Minimum City Distance",     &m_create_parameters.minimum_city_distance,     drag_speed, 1);
    ImGui::DragInt("Number of Starting Cities", &m_create_parameters.number_of_starting_cities, drag_speed, 1);
    ImGui::DragInt("Protection Turn Count",     &m_create_parameters.protection_turn_count,     drag_speed, 0);
    ImGui::DragInt("Starting Technology Level", &m_create_parameters.starting_technology_level, drag_speed, 1);
    ImGui::Checkbox("Reveal Map", &m_create_parameters.reveal_map);

    if (
        (m_number_of_coastal_cities > m_create_parameters.number_of_coastal_cities) ||
        (m_number_of_land_cities > m_create_parameters.number_of_land_cities)
    )
    {
        m_cities.clear();
        m_number_of_coastal_cities = 0;
        m_number_of_land_cities = 0;
        m_player_start_cities.clear();
        m_minimum_player_start_city_distance = 0;
    }

    if (m_number_of_coastal_cities < m_create_parameters.number_of_coastal_cities)
    {
        for (
            int i = 0;
            (m_number_of_coastal_cities < m_create_parameters.number_of_coastal_cities) && (i < 1000);
            ++i
        )
        {
            if (try_create_city(1u << Terrain_flags::bit_can_place_coastal_city))
            {
                ++m_number_of_coastal_cities;
            }
        }
    }

    if (m_number_of_land_cities < m_create_parameters.number_of_land_cities)
    {
        for (
            int i = 0;
            (m_number_of_land_cities < m_create_parameters.number_of_land_cities) && (i < 1000);
            ++i
            )
        {
            if (try_create_city(1u << Terrain_flags::bit_can_place_land_city))
            {
                ++m_number_of_land_cities;
            }
        }
    }

    for (int i = 0; i < 1000; ++i)
    {
        select_player_start_cities();
    }

    ImGui::Text("Generated coastal cities: %d",           m_number_of_coastal_cities);
    ImGui::Text("Generated land cities: %d",              m_number_of_land_cities);
    ImGui::Text("Minimum player start city distance: %d", m_minimum_player_start_city_distance);

    if (ImGui::Button("Start"))
    {
        create();
        hide();
        get<Map_window >()->show();
        get<Game_window>()->show();
    }

    if (ImGui::Button("Back to Menu"))
    {
        hide();
        get<Menu_window>()->show();
    }
}

} // namespace hextiles
