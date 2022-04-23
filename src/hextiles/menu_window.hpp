#pragma once

#include "coordinate.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/imgui_window.hpp"

#include "erhe/components/components.hpp"

namespace hextiles
{

class Menu_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Menu_window"};
    static constexpr std::string_view c_title{"Menu"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Menu_window ();
    ~Menu_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;
};

class New_game_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_name {"New_game_window"};
    static constexpr std::string_view c_title{"New Game"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    New_game_window ();
    ~New_game_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

private:
    auto is_too_close_to_city      (Tile_coordinate location) const -> bool;
    auto try_create_city           (uint32_t flags_match) -> bool;
    void place_cities              ();
    void select_player_start_cities();
    void create                    ();

    std::vector<std::string> m_players;
    std::vector<size_t>      m_start_cities;

    struct Create_parameters
    {
        int  number_of_coastal_cities {10};
        int  number_of_land_cities    {10};
        int  minimum_city_distance    {10};
        int  number_of_starting_cities{1};
        int  protection_turn_count    {2};
        int  starting_technology_level{1};
        bool reveal_map{false};
    };

    Create_parameters            m_create_parameters;
    std::vector<Tile_coordinate> m_cities;
    int                          m_number_of_coastal_cities{0};
    int                          m_number_of_land_cities   {0};

    std::vector<size_t>          m_player_start_cities;
    int                          m_minimum_player_start_city_distance;
};

} // namespace hextiles
