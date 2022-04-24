#pragma once

#include "coordinate.hpp"
#include "map.hpp"
#include "types.hpp"

#include "erhe/components/components.hpp"

#include <string>

namespace hextiles
{

class City
{
public:
    Tile_coordinate location;
    std::string     name;
    unit_t          production;
    int             production_progress;
};

class Unit
{
public:
    Tile_coordinate location;
    unit_t          type{0};
    int             hp  {0};
    int             mp  {0};
    int             fuel{0};
    bool            ready_to_load{false};
    int             level{0};
};

class Player
{
public:
    explicit Player(const std::string& name);
    ~Player() noexcept;

    Player           (const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player           (Player&& other);
    Player& operator=(Player&& other);

    void set_map_size(int width, int height);
    void add_city    (Tile_coordinate location);

private:
    std::unique_ptr<Map> m_map;
    std::string          m_name;
    std::vector<City>    m_cities;
    std::vector<Unit>    m_units;
    size_t               m_city_counter{0};
};

class Map_renderer;
class Tiles;

class Game
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Game"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Game ();
    ~Game() noexcept override;

    void new_game      ();
    void add_player    (const std::string& name);
    void next_turn     ();
    auto current_player() -> Player&;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

private:
    std::shared_ptr<Map_renderer> m_map_renderer;
    std::shared_ptr<Tiles>        m_tiles;

    int                 m_turn  {0};
    size_t              m_player{0};
    std::vector<Player> m_players;
};

} // namespace hextiles
