#pragma once

#include "coordinate.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"

#include "etl/string.h"
#include "etl/vector.h"

namespace hextiles
{

class New_game_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"New_game_window"};
    static constexpr std::string_view c_title{"New Game"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    New_game_window ();
    ~New_game_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

private:
    auto is_too_close_to_city      (Tile_coordinate location) const -> bool;
    auto try_create_city           (uint32_t flags_match) -> bool;
    void place_cities              ();
    void select_player_start_cities();
    void create                    ();

    etl::vector<etl::string<max_name_length>, max_player_count> m_player_names;
    etl::vector<size_t, max_city_count>       m_start_cities;

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

    Create_parameters                            m_create_parameters;
    etl::vector<Tile_coordinate, max_city_count> m_cities;
    int                                          m_number_of_coastal_cities{0};
    int                                          m_number_of_land_cities   {0};

    etl::vector<size_t, max_player_count> m_player_start_cities;
    int                                   m_minimum_player_start_city_distance{10};
};

extern New_game_window* g_new_game_window;

} // namespace hextiles
