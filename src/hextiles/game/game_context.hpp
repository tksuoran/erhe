#pragma once

namespace hextiles
{

class Game;
class Map_window;
class Rendering;
class Tiles;
class Tile_renderer;

struct Game_context
{
    Game&          game;
    Map_window&    map_window;
    Rendering&     rendering;
    Tiles&         tiles;
    Tile_renderer& tile_renderer;
    double         time;
};

} // namespace hextiles
