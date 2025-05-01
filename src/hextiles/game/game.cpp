#include "game/game.hpp"

#include "map.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"
#include "game/game.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

auto Move_unit_command::try_call() -> bool
{
    m_game.move_unit(m_direction);
    return true;
}

auto Select_unit_command::try_call() -> bool
{
    m_game.select_unit(m_direction);
    return true;
}

Game::Game(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Map_window&                  map_window,
    Menu_window&                 menu_window,
    Tile_renderer&               tile_renderer,
    Tiles&                       tiles
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Game", "game"}
    , m_map_window{map_window}
    , m_menu_window{menu_window}
    , m_tile_renderer{tile_renderer}
    , m_tiles{tiles}
    , m_move_unit_n_command         {commands, *this, direction_north     }
    , m_move_unit_ne_command        {commands, *this, direction_north_east}
    , m_move_unit_se_command        {commands, *this, direction_south_east}
    , m_move_unit_s_command         {commands, *this, direction_south     }
    , m_move_unit_sw_command        {commands, *this, direction_south_west}
    , m_move_unit_nw_command        {commands, *this, direction_north_west}
    , m_select_previous_unit_command{commands, *this, -1}
    , m_select_next_unit_command    {commands, *this, 1}
{
    commands.register_command(&m_move_unit_n_command );
    commands.register_command(&m_move_unit_ne_command);
    commands.register_command(&m_move_unit_se_command);
    commands.register_command(&m_move_unit_s_command );
    commands.register_command(&m_move_unit_sw_command);
    commands.register_command(&m_move_unit_nw_command);
    commands.register_command(&m_select_previous_unit_command);
    commands.register_command(&m_select_next_unit_command);
    commands.bind_command_to_key(&m_move_unit_n_command ,         erhe::window::Key_home,      false);
    commands.bind_command_to_key(&m_move_unit_ne_command,         erhe::window::Key_page_up,   false);
    commands.bind_command_to_key(&m_move_unit_se_command,         erhe::window::Key_page_down, false);
    commands.bind_command_to_key(&m_move_unit_s_command ,         erhe::window::Key_end,       false);
    commands.bind_command_to_key(&m_move_unit_sw_command,         erhe::window::Key_delete,    false);
    commands.bind_command_to_key(&m_move_unit_nw_command,         erhe::window::Key_insert,    false);
    commands.bind_command_to_key(&m_select_previous_unit_command, erhe::window::Key_left,      false);
    commands.bind_command_to_key(&m_select_next_unit_command    , erhe::window::Key_right,     false);

    hide_window();
}

auto Game::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoNavInputs;
}

void Game::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    Player& player = get_current_player();
    ImGui::Text("Player: %s", player.name.c_str());
    if (ImGui::Button("End Turn", button_size)) {
        next_turn();
    }

    if (ImGui::Button("Back to Menu", button_size)) {
        hide_window();
        m_map_window.set_map(nullptr);
        m_map_window.hide_window();
        m_menu_window.show_window();
    }

    player_imgui();

    // Blink (highlight) currently selected unit
    // Advance production for all cities
    // Select city production next/prev unit type
    // Select next unit
    // Set unit on hold
    // Move selected unit
    //      attack unit
    //      capture city
    //      rename city
    // Build - Fortify
    // Build - Road
    // Build - Bridge
    // Build - Plain
    // Build - Field
    // Kill unit (suicide)
    // Set/unset unit ready for loading
    // Ranged attack
    // Cycle grid modes
    // Toggle battletype (surface/submerged)

    // unit statistics window
    // unit production window
}

void Game::player_imgui()
{
    Player& player = get_current_player();
    constexpr ImVec2 button_size{110.0f, 0.0f};

    if (ImGui::Button("Next Unit", button_size)) {
        select_player_unit(1);
    }

    if (ImGui::Button("Previous Unit", button_size)) {
        select_player_unit(-1);
    }

    if (player.current_unit < player.cities.size()) {
        city_imgui();
    } else {
        unit_imgui();
    }
}

void Game::city_imgui()
{
    Player& player = get_current_player();

    const size_t      city_index = player.current_unit;
    Unit&             city       = player.cities.at(city_index);
    const unit_tile_t unit_tile  = m_tile_renderer.get_single_unit_tile(player.id, city.type);
    const Unit_type&  product    = m_tiles.get_unit_type(city.production);

    ImGui::Text("City %zu", city_index);
    ImGui::SameLine();
    m_tile_renderer.unit_image(unit_tile, 2);
    ImGui::SameLine();
    ImGui::InputText("##city_name", city.name.data(), city.name.capacity());
    ImGui::Text("%d, %d", city.location.x, city.location.y);
    m_tile_renderer.make_unit_type_combo("Product", city.production, player.id);
    ImGui::Text("PT: %d/%d", city.production_progress, product.production_time);
}

void Game::unit_imgui()
{
    Player& player = get_current_player();

    const size_t      unit_index = player.current_unit - player.cities.size();
    Unit&             unit       = player.units.at(unit_index);
    const unit_tile_t unit_tile  = m_tile_renderer.get_single_unit_tile(player.id, unit.type);
    const Unit_type&  unit_type  = m_tiles.get_unit_type(unit.type);

    ImGui::Text("Unit %zu", unit_index);
    ImGui::SameLine();
    m_tile_renderer.unit_image(unit_tile, 2);
    ImGui::SameLine();
    ImGui::Text("%s", unit_type.name.c_str());
    ImGui::Text("%d, %d", unit.location.x, unit.location.y);
    ImGui::Text("Hit Points: %d / %d", unit.hit_points, unit_type.hit_points);
    ImGui::Text("Move Points: %d / %d", unit.move_points, unit_type.move_points[0]);
    ImGui::Text("Fuel: %d / %d", unit.fuel, unit_type.fuel);
}

void Game::animate_current_unit()
{
    Player& player = get_current_player();

    const double      sin_time         = std::sin(get_time_now() * 10.0f);
    const bool        show_active_unit = sin_time > 0.0f;
    const size_t      unit_index       = player.current_unit - player.cities.size();
    const Unit&       unit             = player.units.at(unit_index);
    const unit_tile_t unit_tile =
        show_active_unit
            ? m_tile_renderer.get_single_unit_tile(player.id, unit.type)
            : get_unit_tile(unit.location, &unit);
    player.map.set_unit_tile(unit.location, unit_tile);
}

void Game::update_player()
{
    Player& player = get_current_player();
    if (player.current_unit < player.cities.size()) {
        return; // TODO
    }

    if (player.move.has_value()) {
        const Player::Move&   move         = player.move.value();
        const size_t          unit_index   = player.current_unit - player.cities.size();
        Unit&                 unit         = player.units.at(unit_index);
        const Unit_type&      unit_type    = m_tiles.get_unit_type(unit.type);
        const Tile_coordinate old_location = unit.location;
        const Tile_coordinate new_location = move.target;

        unit.location = new_location;
        update_map_unit_tile(old_location);
        update_map_unit_tile(new_location);
        reveal(player.map, old_location, unit_type.vision_range[0]);
        reveal(player.map, new_location, unit_type.vision_range[0]);
        player.move.reset();
    }
}

void Game::move_player_unit(const direction_t direction)
{
    Player& player = get_current_player();
    // Cities can not be moved
    if (player.current_unit < player.cities.size()) {
        return;
    }

    const size_t          unit_index   = player.current_unit - player.cities.size();
    Unit&                 unit         = player.units.at(unit_index);
    const Unit_type&      unit_type    = m_tiles.get_unit_type(unit.type);
    const Tile_coordinate old_location = unit.location;
    const Tile_coordinate new_location = player.map.wrap(old_location.neighbor(direction));

    const unit_tile_t unit_tile = get_unit_tile(unit.location, &unit);
    player.map.set_unit_tile(unit.location, unit_tile);

    player.move = Player::Move{
        .target     = new_location,
        .start_time = get_time_now()
    };

    unit.location = new_location;
    update_map_unit_tile(old_location);
    update_map_unit_tile(new_location);
    reveal(player.map, old_location, unit_type.vision_range[0]);
    reveal(player.map, new_location, unit_type.vision_range[0]);
}

void Game::select_player_unit(const int direction)
{
    Player& player = get_current_player();
    if (player.current_unit >= player.cities.size()) {
        const size_t unit_index = player.current_unit - player.cities.size();
        Unit& unit = player.units.at(unit_index);
        reveal(player.map, unit.location, 0);
    }

    if (direction > 0) {
        ++player.current_unit;
        if (player.current_unit >= (player.cities.size() + player.units.size())) {
            player.current_unit = 0;
        }
    } else {
        if (player.current_unit > 0) {
            --player.current_unit;
        } else {
            player.current_unit = player.cities.size() + player.units.size() - 1;
        }
    }
}

void Game::apply_player_fog_of_war()
{
    Player& player = get_current_player();

    // Step 1: Half-hide explored map cells
    const coordinate_t width      = static_cast<coordinate_t>(player.map.width());
    const coordinate_t height     = static_cast<coordinate_t>(player.map.height());
    const unit_tile_t  fog_of_war = m_tile_renderer.get_special_unit_tile(Special_unit_tiles::fog_of_war);
    const unit_tile_t  half_fog   = m_tile_renderer.get_special_unit_tile(Special_unit_tiles::half_fog_of_war);
    for (coordinate_t y = 0; y < height; ++y) {
        for (coordinate_t x = 0; x < width; ++x) {
            const Tile_coordinate position{x, y};
            const unit_tile_t     old_unit_tile = player.map.get_unit_tile(position);
            if (old_unit_tile != fog_of_war) {
                player.map.set_unit_tile(
                    Tile_coordinate{x, y},
                    half_fog
                );
            }
        }
    }

    // Step 2: Reveal map cells that can be seen
    for (Unit& city : player.cities) {
        const Unit_type& unit_type = m_tiles.get_unit_type(city.type);
        reveal(player.map, city.location, unit_type.vision_range[0]);
    }
    for (Unit& unit : player.units) {
        const Unit_type& unit_type = m_tiles.get_unit_type(unit.type);
        reveal(player.map, unit.location, unit_type.vision_range[0]);
    }
}

void Game::update_player_units()
{
    Player& player = get_current_player();

    for (Unit& unit : player.units) {
        const Unit_type& unit_type = m_tiles.get_unit_type(unit.type);
        unit.move_points = unit_type.move_points[0];
        // TODO repair and refuel if in city or carrier
    }
}

void Game::update_player_cities()
{
    Player& player = get_current_player();

    // Progress production
    for (Unit& city : player.cities) {
        if (city.production == unit_t{0}) {
            continue;
        }
        //Unit_type& product = context.s.tiles.get_unit_type(city.production);
        ++city.production_progress;
        if (city.production_progress >= 1) { //product.production_time)
            Unit unit = make_unit(city.production, city.location);
            player.units.push_back(unit);
            city.production_progress = 0;
        }
    }
}

void Game::update_map_unit_tile(Tile_coordinate position)
{
    const unit_tile_t unit_tile = get_unit_tile(position);
    m_map->set_unit_tile(position, unit_tile);
}

void Game::reveal(Map& target_map, Tile_coordinate position, int radius) const
{
    std::function<void(Tile_coordinate)> reveal_op =
    [this, &target_map] (Tile_coordinate position) -> void
    {
        const terrain_tile_t terrain_tile = m_map->get_terrain_tile(position);
        const unit_tile_t    unit_tile    = m_map->get_unit_tile(position);
        target_map.set(position, terrain_tile, unit_tile);
    };

    m_map->hex_circle(position, 0, radius, reveal_op);
}

/// void Game::update_once_per_frame(const Time_context& time_context)
/// {
///     m_frame_time = time_context.time;
///     if (m_players.empty()) {
///         return;
///     }
/// 
///     Game_context context = get_context();
///     get_current_player().update(context);
/// }

auto Game::move_unit(const direction_t direction) -> bool
{
    if (m_players.empty()) {
        return false;
    }

    if (is_window_visible() == false) {
        return false;
    }

    move_player_unit(direction);
    return true;
}

auto Game::select_unit(const int direction) -> bool
{
    if (m_players.empty()) {
        return false;
    }

    if (is_window_visible() == false) {
        return false;
    }

    select_player_unit(direction);

    return true;
}

auto Game::get_unit_tile(
    const Tile_coordinate position,
    const Unit*           ignore
) -> unit_tile_t
{
    uint32_t    occupied_player_mask     {0u};
    //uint32_t    occupied_battle_type_mask{0u};
    unit_tile_t first_found_unit_tile    {0u};
    size_t      found_unit_count         {0u};
    std::array<int, Battle_type::bit_count> battle_type_player_id;

    battle_type_player_id.fill(0);

    //std::vector<Player_unit> player_units;

    for (int player_index = 0; player_index < m_players.size(); ++player_index) {
        const int player_id = player_index + 1;
        const Player& player = m_players[player_index];
        for (const Unit& unit : player.cities) {
            if (unit.location != position) {
                continue;
            }
            //const Unit_type& unit_type = m_tiles.get_unit_type(unit.type);
            //occupied_battle_type_mask |= (1u << unit_type.battle_type);
            // Single city found
            return m_tile_renderer.get_single_unit_tile(player_index, unit.type);
        }

        for (const Unit& unit : player.units) {
            if (unit.location != position) {
                continue;
            }
            if (&unit == ignore) {
                continue;
            }
            const Unit_type& unit_type = m_tiles.get_unit_type(unit.type);
            //occupied_battle_type_mask |= (1u << unit_type.battle_type);
            occupied_player_mask      |= (1u << player_index);

            //ERHE_VERIFY(
            //    (battle_type_player_id[unit_type.battle_type] == 0) ||
            //    (battle_type_player_id[unit_type.battle_type] == player_id)
            //);                        //
            battle_type_player_id[unit_type.battle_type] = player_id;
            if (found_unit_count == 0) {
                first_found_unit_tile = m_tile_renderer.get_single_unit_tile(player_index, unit.type);
            }
            ++found_unit_count;
        }
    }

    if (found_unit_count == 0) {
        return m_tile_renderer.get_special_unit_tile(7);
    }
    if (found_unit_count == 1) {
        return first_found_unit_tile;
    }
    return m_tile_renderer.get_multi_unit_tile(battle_type_player_id);
}

auto Game::get_time_now() const -> float
{
    return 0.0f; // TODO
}

auto Game::make_unit(
    const unit_t          unit_id,
    const Tile_coordinate location
) -> Unit
{
    Unit_type&        unit_type = m_tiles.get_unit_type(unit_id);
    const unit_tile_t unit_tile = get_unit_tile(location);
    m_map->set_unit_tile(location, unit_tile);
    return Unit{
        .location            = location,
        .type                = unit_id,
        .hit_points          = unit_type.hit_points,
        .move_points         = unit_type.move_points[0],
        .fuel                = unit_type.fuel,
        .ready_to_load       = false,
        .level               = 0,
        .name                = "unit", // TODOfmt::format("{} {} {}"),
        .production          = unit_t{0},
        .production_progress = 0,
    };
}

void Game::add_player(const std::string& name, const Tile_coordinate location)
{
    const terrain_tile_t city_terrain_tile = m_map->get_terrain_tile(location);
    const terrain_t      city_terrain      = m_tiles.get_terrain_from_tile(city_terrain_tile);
    const Terrain_type&  city_terrain_type = m_tiles.get_terrain_type     (city_terrain);
    const unit_t         city_unit_id      = m_tiles.get_city_unit_type   (city_terrain_type.city_size);
    const int            player_id         = static_cast<int>(m_players.size());
    const unit_tile_t    unit_tile         = m_tile_renderer.get_single_unit_tile(player_id, city_unit_id);

    m_players.emplace_back();
    Player& player = m_players.back();
    player.id   = player_id;
    player.name = name;
    player.map.reset(m_map->width(), m_map->height());

    Unit city = make_unit(city_unit_id, location);
    player.cities.push_back(city);

    auto& player_map = player.map;
    m_map->set_unit_tile(location, unit_tile);
    reveal(player_map, location, city_terrain_type.city_size);
}

void Game::new_game(const Game_create_parameters& parameters)
{
    m_players.clear();
    m_map            = parameters.map;
    m_current_player = 0;
    m_turn           = 0;
    m_cities         = parameters.city_positions;
    for (size_t i = 0; i < parameters.player_names.size(); ++i) {
        add_player(
            parameters.player_names[i],
            parameters.city_positions[parameters.player_starting_cities[i]]
        );
    }
    m_current_player = 0;
    update_current_player();
}

void Game::next_turn()
{
    ERHE_VERIFY(!m_players.empty());

    m_current_player = (m_current_player + 1) % m_players.size();
    update_current_player();
}

void Game::update_current_player()
{
    auto& player     = get_current_player();
    auto& first_city = player.cities.at(0);
    m_map_window.set_map  (&player.map);
    m_map_window.scroll_to(first_city.location);

    apply_player_fog_of_war();
    update_player_units    ();
    update_player_cities   ();
}

auto Game::get_current_player() -> Player&
{
    ERHE_VERIFY(!m_players.empty());
    return m_players.at(m_current_player);
}

} // namespace hextiles
