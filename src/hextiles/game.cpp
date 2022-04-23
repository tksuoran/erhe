#include "game.hpp"
#include "game_window.hpp"
#include "map.hpp"
#include "map_renderer.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "tiles.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

Player::Player(const std::string& name)
    : m_name{name}
{
}

Player::~Player() noexcept
{
}

Player::Player(Player&& other)
    : m_map         {std::move(other.m_map   )}
    , m_name        {std::move(other.m_name  )}
    , m_cities      {std::move(other.m_cities)}
    , m_units       {std::move(other.m_units )}
    , m_city_counter{other.m_city_counter}
{
}

Player& Player::operator=(Player&& other)
{
    m_map          = std::move(other.m_map   );
    m_name         = std::move(other.m_name  );
    m_cities       = std::move(other.m_cities);
    m_units        = std::move(other.m_units );
    m_city_counter = other.m_city_counter;
    return *this;
}

void Player::set_map_size(int width, int height)
{
    m_map = std::make_unique<Map>();
    m_map->reset(width, height);
}

void Player::add_city(Tile_coordinate location)
{
    City city{
        .location            = location,
        .name                = fmt::format("City {}", ++m_city_counter),
        .production          = unit_t{1},
        .production_progress = 0,
    };

    m_cities.push_back(city);
}

Game::Game()
    : erhe::components::Component{c_name}
{
}

Game::~Game()
{
}

void Game::connect()
{
    m_map_renderer = get<Map_renderer>();
    m_tiles        = get<Tiles       >();
}

void Game::initialize_component()
{
}

void Game::new_game()
{
    m_turn = 0;
    m_player = 0;
    m_players.clear();
}

void Game::add_player(const std::string& name)
{
    m_players.emplace_back(name);
}

void Game::next_turn()
{
    m_player = (m_player + 1) % m_players.size();
}

auto Game::current_player() -> Player&
{
    return m_players.at(m_player);
}

} // namespace hextiles
