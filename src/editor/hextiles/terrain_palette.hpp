#pragma once

#include "area.hpp"

namespace power_battle
{

class Game;

class Terrain_palette
    : public Area
{
public:
    Game* m_game;

    void mouse_event(
        bool             left_mouse,
        bool             right_mouse,
        Pixel_coordinate position
    );
    void draw() const;
};

}
