#include "menu_window.hpp"

#include "new_game_window.hpp"
#include "hextiles_log.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"
#include "game/game.hpp"
#include "map_editor/map_editor.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles {

New_game_window::New_game_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Game&                        game,
    Map_editor&                  map_editor,
    Map_window&                  map_window,
    Menu_window&                 menu_window,
    Tile_renderer&               tile_renderer,
    Tiles&                       tiles
)
    : Imgui_window   {imgui_renderer, imgui_windows, "New Game", "new_game"}
    , m_game         {game}
    , m_map_editor   {map_editor}
    , m_map_window   {map_window}
    , m_menu_window  {menu_window}
    , m_tile_renderer{tile_renderer}
    , m_tiles        {tiles}
{
    m_player_names.clear();
    m_player_names.push_back("Player 1");
    m_player_names.push_back("Player 2");
    hide_window();
}

namespace {

auto random_coordinate(coordinate_t w, coordinate_t h) -> Tile_coordinate
{
    const coordinate_t x = rand() % w;
    const coordinate_t y = rand() % h;
    return Tile_coordinate{x, y};
}

}

auto New_game_window::is_too_close_to_city(Tile_coordinate location) const -> bool
{
    Map* map = m_map_editor.get_map();
    for (auto& city : m_cities) {
        auto distance = map->distance(city, location);
        if (distance < m_create_parameters.minimum_city_distance) {
            return true;
        }
    }
    return false;
}

auto New_game_window::try_create_city(const uint32_t flags_match) -> bool
{
    Map* map = m_map_editor.get_map();

    const auto w = static_cast<coordinate_t>(map->width());
    const auto h = static_cast<coordinate_t>(map->height());

    const Tile_coordinate position     = random_coordinate(w, h);
    const terrain_tile_t  terrain_tile = map->get_terrain_tile(position);
    const terrain_t       terrain      = m_tiles.get_terrain_from_tile(terrain_tile);
    const Terrain_type    terrain_type = m_tiles.get_terrain_type(terrain);

    //if (terrain.city_size == terrain_city_size_match) {
    //    log_new_game.info("{}, {} {}: {}\n", position.x, position.y, terrain.name, terrain.city_size);
    //}

    if ((terrain_type.flags & flags_match) != flags_match) {
        return false;
    }

    if (is_too_close_to_city(position)) {
        return false;
    }

    m_cities.push_back(position);
    return true;
}

void New_game_window::select_player_start_cities()
{
    if (m_cities.empty()) {
        return;
    }

    // TODO for now, map editor is used to select the map
    Map* map = m_map_editor.get_map();

    etl::vector<size_t, max_player_count> player_start_cities;
    int minimum_distance = 99999999;
    for (size_t player = 0; player < m_player_names.size(); ++player) {
        size_t city_id = rand() % m_cities.size();
        player_start_cities.push_back(city_id);
    }
    for (size_t player_a = 0; player_a < m_player_names.size() - 1; ++player_a) {
        const auto& player_a_city = m_cities[player_start_cities[player_a]];
        for (size_t player_b = player_a + 1; player_b < m_player_names.size(); ++player_b) {
            const auto& player_b_city = m_cities[player_start_cities[player_b]];
            int distance = map->distance(player_a_city, player_b_city);
            minimum_distance = std::min(minimum_distance, distance);
        }
    }
    if (minimum_distance > m_minimum_player_start_city_distance) {
        m_minimum_player_start_city_distance = minimum_distance;
        m_player_start_cities = player_start_cities;
    }
}

void New_game_window::create()
{
    Map* map = m_map_editor.get_map();

    place_cities();

    m_game.new_game(
        Game_create_parameters{
            .map                    = map,
            .player_names           = m_player_names,
            .player_starting_cities = m_player_start_cities,
            .city_positions         = m_cities
        }
    );
}

void New_game_window::place_cities()
{
    Map* map = m_map_editor.get_map();

    std::vector<terrain_tile_t> city_tiles;

    const auto end = m_tiles.get_terrain_type_count();
    for (terrain_t i = 0; i < end; ++i) {
        const auto& terrain = m_tiles.get_terrain_type(i);
        if (terrain.city_size > 0) {
            const terrain_tile_t city_terrain_tile = m_tiles.get_terrain_tile_from_terrain(i);
            city_tiles.push_back(city_terrain_tile);
        }
    }

    ERHE_VERIFY(!city_tiles.empty());

    for (auto& city_location : m_cities) {
        int city_tile = rand() % city_tiles.size();
        map->set_terrain_tile(city_location, city_tiles.at(city_tile));

        const terrain_tile_t terrain_tile = map->get_terrain_tile(city_location);
        const terrain_t      terrain      = m_tiles.get_terrain_from_tile(terrain_tile);
        const Terrain_type&  terrain_type = m_tiles.get_terrain_type(terrain);

        int field_count = terrain_type.city_size;
        map->hex_circle(
            city_location,
            0,
            4,
            [this ,&map, &field_count](const Tile_coordinate position)
            {
                if (field_count == 0) {
                    return;
                }

                const terrain_tile_t terrain_tile = map->get_terrain_tile(position);
                const terrain_t      terrain      = m_tiles.get_terrain_from_tile(terrain_tile);
                const Terrain_type&  terrain_type = m_tiles.get_terrain_type(terrain);
                if ((terrain_type.flags & Terrain_flags::bit_can_place_field) == Terrain_flags::bit_can_place_field) {
                    map->set_terrain_tile(position, m_tiles.get_terrain_tile_from_terrain(Terrain_field));
                    --field_count;
                }
            }
        );
    }
}

namespace {

void readonly_int(const char* label, int value)
{
    ImGui::DragInt(label, &value, 0, value, value, "%d", ImGuiSliderFlags_NoInput);
}

}

void New_game_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    int player_number    = 1;
    int player_to_remove = 0;
    for (auto& player_name : m_player_names) {
        m_tile_renderer.unit_image(m_tile_renderer.get_single_unit_tile(player_number - 1, 2), 2);
        ImGui::SameLine();

        auto label = fmt::format("Name of player {}", player_number);
        // TODO Fix string input text
        ImGui::InputText(label.c_str(), &player_name);
        if (m_player_names.size() > 2) {
            ImGui::SameLine();
            auto remove_button_label = fmt::format("Remove##remove-player-{}", player_number);
            if (ImGui::SmallButton(remove_button_label.c_str())) {
                player_to_remove = player_number;
            }
        }
        ++player_number;
    }
    if (player_to_remove > 0) {
        m_player_names.erase(m_player_names.begin() + player_to_remove - 1);
        m_player_start_cities.clear();
        m_minimum_player_start_city_distance = 0;
    }
    if (m_player_names.size() < max_player_count) {
        if (ImGui::SmallButton("Add Player")) {
            m_player_names.push_back("Mr. Smith");
            m_player_start_cities.clear();
            m_minimum_player_start_city_distance = 0;
        }
    }

    ImGui::PushItemWidth(80.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

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
    ) {
        m_cities.clear();
        m_number_of_coastal_cities = 0;
        m_number_of_land_cities = 0;
        m_player_start_cities.clear();
        m_minimum_player_start_city_distance = 0;
    }

    if (m_number_of_coastal_cities < m_create_parameters.number_of_coastal_cities) {
        for (
            int i = 0;
            (m_number_of_coastal_cities < m_create_parameters.number_of_coastal_cities) && (i < 1000);
            ++i
        ) {
            if (try_create_city(1u << Terrain_flags::bit_can_place_coastal_city)) {
                ++m_number_of_coastal_cities;
            }
        }
    }

    if (m_number_of_land_cities < m_create_parameters.number_of_land_cities) {
        for (
            int i = 0;
            (m_number_of_land_cities < m_create_parameters.number_of_land_cities) && (i < 1000);
            ++i
        ) {
            if (try_create_city(1u << Terrain_flags::bit_can_place_land_city)) {
                ++m_number_of_land_cities;
            }
        }
    }

    for (int i = 0; i < 1000; ++i) {
        select_player_start_cities();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    readonly_int("Generated coastal cities",           m_number_of_coastal_cities);
    readonly_int("Generated land cities",              m_number_of_land_cities);
    readonly_int("Minimum player start city distance", m_minimum_player_start_city_distance);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Start", button_size)) {
        create();
        hide_window();
        m_map_window.show_window();
        m_game.show_window();
    }

    if (ImGui::Button("Back to Menu", button_size)) {
        m_menu_window.show_menu();
    }

    ImGui::PopItemWidth();
}

} // namespace hextiles
