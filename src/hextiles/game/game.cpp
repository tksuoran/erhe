#include "map.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "rendering.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"
#include "game/game.hpp"
#include "game/game_window.hpp"

#include "erhe/application/time.hpp"
#include "erhe/application/commands/commands.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

auto Move_unit_command::try_call(erhe::application::Command_context&) -> bool
{
    m_game.move_unit(m_direction);
    return true;
}

auto Select_unit_command::try_call(erhe::application::Command_context&) -> bool
{
    m_game.select_unit(m_direction);
    return true;
}

Game::Game()
    : erhe::components::Component{c_type_name}
    , m_move_unit_n_command {*this, direction_north     }
    , m_move_unit_ne_command{*this, direction_north_east}
    , m_move_unit_se_command{*this, direction_south_east}
    , m_move_unit_s_command {*this, direction_south     }
    , m_move_unit_sw_command{*this, direction_south_west}
    , m_move_unit_nw_command{*this, direction_north_west}
    , m_select_previous_unit_command{*this, -1}
    , m_select_next_unit_command    {*this, 1}
{
}

Game::~Game() noexcept
{
}

void Game::declare_required_components()
{
    require<erhe::application::Commands>();
}

void Game::initialize_component()
{
    const auto commands = get<erhe::application::Commands>();

    commands->register_command(&m_move_unit_n_command );
    commands->register_command(&m_move_unit_ne_command);
    commands->register_command(&m_move_unit_se_command);
    commands->register_command(&m_move_unit_s_command );
    commands->register_command(&m_move_unit_sw_command);
    commands->register_command(&m_move_unit_nw_command);
    commands->register_command(&m_select_previous_unit_command);
    commands->register_command(&m_select_next_unit_command);

    commands->bind_command_to_key(&m_move_unit_n_command ,         erhe::toolkit::Key_home,      false);
    commands->bind_command_to_key(&m_move_unit_ne_command,         erhe::toolkit::Key_page_up,   false);
    commands->bind_command_to_key(&m_move_unit_se_command,         erhe::toolkit::Key_page_down, false);
    commands->bind_command_to_key(&m_move_unit_s_command ,         erhe::toolkit::Key_end,       false);
    commands->bind_command_to_key(&m_move_unit_sw_command,         erhe::toolkit::Key_delete,    false);
    commands->bind_command_to_key(&m_move_unit_nw_command,         erhe::toolkit::Key_insert,    false);
    commands->bind_command_to_key(&m_select_previous_unit_command, erhe::toolkit::Key_left,      false);
    commands->bind_command_to_key(&m_select_next_unit_command    , erhe::toolkit::Key_right,     false);
}

void Game::post_initialize()
{
    m_time          = get<erhe::application::Time>();
    m_map_window    = get<Map_window   >();
    m_rendering     = get<Rendering    >();
    m_tiles         = get<Tiles        >();
    m_tile_renderer = get<Tile_renderer>();
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

auto Game::get_context() -> Game_context
{
    return Game_context
    {
        .game          = *this,
        .map_window    = *m_map_window.get(),
        .rendering     = *m_rendering.get(),
        .tiles         = *m_tiles.get(),
        .tile_renderer = *m_tile_renderer.get(),
        .time          = m_frame_time
    };
}

void Game::update_once_per_frame(const erhe::components::Time_context& time_context)
{
    m_frame_time = time_context.time;
    if (m_players.empty())
    {
        return;
    }

    Game_context context = get_context();
    get_current_player().update(context);
}

auto Game::move_unit(direction_t direction) -> bool
{
    if (m_players.empty())
    {
        return false;
    }
    if (get<Game_window>()->is_visible() == false)
    {
        return false;
    }

    Game_context context = get_context();
    get_current_player().move_unit(
        context,
        direction
    );
    return true;
}

auto Game::select_unit(int direction) -> bool
{
    if (m_players.empty())
    {
        return false;
    }
    if (get<Game_window>()->is_visible() == false)
    {
        return false;
    }

    Game_context context = get_context();
    get_current_player().select_unit(context, direction);

    return true;
}

auto Game::get_unit_tile(Tile_coordinate position, const Unit* ignore) -> unit_tile_t
{
    uint32_t    occupied_player_mask     {0u};
    uint32_t    occupied_battle_type_mask{0u};
    unit_tile_t first_found_unit_tile    {0u};
    size_t      found_unit_count         {0u};
    std::array<int, Battle_type::bit_count> battle_type_player_id;

    battle_type_player_id.fill(0);

    //std::vector<Player_unit> player_units;

    for (int player_index = 0; player_index < m_players.size(); ++player_index)
    {
        const int player_id = player_index + 1;
        const Player& player = m_players[player_index];
        for (const Unit& unit : player.cities)
        {
            if (unit.location != position)
            {
                continue;
            }
            const Unit_type& unit_type = m_tiles->get_unit_type(unit.type);
            occupied_battle_type_mask |= (1u << unit_type.battle_type);
            // Single city found
            return m_tile_renderer->get_single_unit_tile(player_index, unit.type);
        }

        for (const Unit& unit : player.units)
        {
            if (unit.location != position)
            {
                continue;
            }
            if (&unit == ignore)
            {
                continue;
            }
            const Unit_type& unit_type = m_tiles->get_unit_type(unit.type);
            occupied_battle_type_mask |= (1u << unit_type.battle_type);
            occupied_player_mask      |= (1u << player_index);

            //Expects(
            //    (battle_type_player_id[unit_type.battle_type] == 0) ||
            //    (battle_type_player_id[unit_type.battle_type] == player_id)
            //);
            battle_type_player_id[unit_type.battle_type] = player_id;
            if (found_unit_count == 0)
            {
                first_found_unit_tile = m_tile_renderer->get_single_unit_tile(player_index, unit.type);
            }
            ++found_unit_count;
        }
    }

    if (found_unit_count == 0)
    {
        return m_tile_renderer->get_special_unit_tile(7);
    }
    if (found_unit_count == 1)
    {
        return first_found_unit_tile;
    }
    return m_tile_renderer->get_multi_unit_tile(
        battle_type_player_id
    );
}

auto Game::make_unit(unit_t unit_id, Tile_coordinate location) -> Unit
{
    Unit_type& unit_type = m_tiles->get_unit_type(unit_id);
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

void Game::add_player(const std::string& name, Tile_coordinate location)
{
    const terrain_tile_t city_terrain_tile = m_map->get_terrain_tile(location);
    const terrain_t      city_terrain      = m_tiles->get_terrain_from_tile(city_terrain_tile);
    const Terrain_type&  city_terrain_type = m_tiles->get_terrain_type(city_terrain);
    const unit_t         city_unit_id      = m_tiles->get_city_unit_type(city_terrain_type.city_size);
    const int            player_id         = static_cast<int>(m_players.size());
    const unit_tile_t    unit_tile         = m_tile_renderer->get_single_unit_tile(player_id, city_unit_id);

    m_players.emplace_back();
    Player& player = m_players.back();
    player.id   = player_id;
    player.name = name;
    player.map  = std::make_shared<Map>();
    player.map->reset(m_map->width(), m_map->height());

    Unit city = make_unit(city_unit_id, location);
    player.cities.push_back(city);

    auto& player_map = *player.map.get();
    m_map->set_unit_tile(location, unit_tile);
    reveal(player_map, location, city_terrain_type.city_size);
}

void Game::new_game(const Game_create_parameters& parameters)
{
    m_players.clear();
    m_current_player = 0;
    m_turn           = 0;
    m_map            = parameters.map;
    m_cities         = parameters.city_positions;
    for (size_t i = 0; i < parameters.player_names.size(); ++i)
    {
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
    m_current_player = (m_current_player + 1) % m_players.size();
    update_current_player();
}

void Game::update_current_player()
{
    auto& player     = get_current_player();
    auto& first_city = player.cities.at(0);
    auto  map_window = get<Map_window>();
    map_window->set_map(player.map);
    map_window->scroll_to(first_city.location);

    Game_context context = get_context();
    player.fog_of_war   (context);
    player.update_units (context);
    player.update_cities(context);
}

auto Game::get_current_player() -> Player&
{
    Expects(!m_players.empty());
    return m_players.at(m_current_player);
}

} // namespace hextiles
