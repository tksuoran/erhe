#pragma once

#include "hextiles.hpp"
#include "coordinate.hpp"
#include "pixel_lookup.hpp"
#include "terrain_type.hpp"
#include "tile_shape.hpp"
#include "types.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_imgui/windows/framebuffer_window.hpp"

#include <numeric>

namespace erhe::commands {
    class Commands;
}
namespace erhe::graphics {
    class Instance;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::renderer {
    class Text_renderer;
}

namespace hextiles {

class Map;
class Map_window;
class Tile_renderer;

class Map_scroll_command final
    : public erhe::commands::Command
{
public:
    Map_scroll_command(
        erhe::commands::Commands& commands,
        Map_window&               map_window,
        float                     dx,
        float                     dy
    );
    auto try_call() -> bool override;

private:
    Map_window& m_map_window;
    glm::vec2   m_offset;
};

class Map_free_zoom_command final : public erhe::commands::Command
{
public:
    Map_free_zoom_command(erhe::commands::Commands& commands, Map_window& map_window);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Map_window& m_map_window;
};

class Map_mouse_scroll_command final : public erhe::commands::Command
{
public:
    Map_mouse_scroll_command(erhe::commands::Commands& commands, Map_window& map_window);

    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Map_window& m_map_window;
};

class Map_zoom_command : public erhe::commands::Command
{
public:
    Map_zoom_command(erhe::commands::Commands& commands, Map_window& map_window, float scal);
    auto try_call() -> bool override;

private:
    Map_window& m_map_window;
    float       m_scale{1.0f};
};

class Map_grid_cycle_command : public erhe::commands::Command
{
public:
    Map_grid_cycle_command(erhe::commands::Commands& commands, Map_window& map_window);

    auto try_call() -> bool override;

private:
    Map_window& m_map_window;
};

class Map_window
    : public erhe::imgui::Framebuffer_window
{
public:
    Map_window(
        erhe::commands::Commands&      commands,
        erhe::graphics::Instance&      graphics_instance,
        erhe::imgui::Imgui_renderer&   imgui_renderer,
        erhe::imgui::Imgui_windows&    imgui_windows,
        erhe::renderer::Text_renderer& text_renderer,
        Tile_renderer&                 tile_renderer
    );

    // Implements Framebuffer_window
    auto get_size(glm::vec2 available_size) const -> glm::vec2 override;

    // Implements Imgui_window
    void imgui               ()                     override; // overrides Framebuffer_window
    auto flags               () -> ImGuiWindowFlags override;
    void on_begin            ()                     override;
    void on_end              ()                     override;
    auto want_keyboard_events() const -> bool       override;
    auto want_mouse_events   () const -> bool       override;

    // Public API
    void set_map(Map* map);

    // Commands
    auto mouse_scroll_try_ready() const -> bool;
    void scroll                (glm::vec2 delta);
    void scroll_tiles          (glm::vec2 delta);
    void grid_cycle            ();

    void scroll_to    (const Tile_coordinate center_tile);

    void render       ();
    void blit         (const Pixel_coordinate shape, const Tile_coordinate tile_location, const uint32_t color = 0xffffffff) const;
    void print        (const std::string_view text, const Tile_coordinate tile_location, uint32_t color = 0xffffffff) const;

    auto wrap         (Tile_coordinate in) const -> Tile_coordinate;
    auto wrap_center  (Tile_coordinate in) const -> Tile_coordinate;
    bool hit_tile     (Tile_coordinate tile_coordinate) const;
    auto pixel_to_tile(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate;
    void scale_zoom   (float scale);
    void set_zoom     (float scale);

    auto tile_image   (terrain_tile_t terrain, const int scale = 1) -> bool;
    auto tile_position(const Tile_coordinate coordinate) const -> glm::vec2;

private:
    auto normalize(Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate;

    // Commands
    erhe::renderer::Text_renderer& m_text_renderer;
    Tile_renderer&                 m_tile_renderer;

    Map_free_zoom_command         m_free_zoom_command;
    Map_mouse_scroll_command      m_mouse_scroll_command;
    Map_scroll_command            m_scroll_left_command;
    Map_scroll_command            m_scroll_right_command;
    Map_scroll_command            m_scroll_up_command;
    Map_scroll_command            m_scroll_down_command;
    Map_zoom_command              m_zoom_in_command;
    Map_zoom_command              m_zoom_out_command;
    Map_grid_cycle_command        m_grid_cycle_command;

    Map*                          m_map         {nullptr};
    Pixel_lookup                  m_pixel_lookup;
    int                           m_grid        {2};
    float                         m_zoom        {1.0f};
    glm::vec2                     m_pixel_offset{0.0f, 0.0f};
    Tile_coordinate               m_center_tile {0, 0};
};

} // namespace hextiles
