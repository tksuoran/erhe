#pragma once

#include "hextiles.hpp"
#include "coordinate.hpp"
#include "pixel_lookup.hpp"
#include "types.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_imgui/windows/framebuffer_window.hpp"

namespace erhe::commands {
    class Commands;
}
namespace erhe::graphics {
    class Device;
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

class Map_scroll_command final : public erhe::commands::Command
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

class Map_window : public erhe::imgui::Framebuffer_window
{
public:
    Map_window(
        erhe::commands::Commands&      commands,
        erhe::graphics::Device&        graphics_device,
        erhe::imgui::Imgui_renderer&   imgui_renderer,
        erhe::imgui::Imgui_windows&    imgui_windows,
        erhe::renderer::Text_renderer& text_renderer,
        Tile_renderer&                 tile_renderer
    );

    // Implements Framebuffer_window
    [[nodiscard]] auto get_size(glm::vec2 available_size) const -> glm::vec2 override;

    // Implements Imgui_window
    void imgui               ()                     override; // overrides Framebuffer_window
    auto flags               () -> ImGuiWindowFlags override;
    void on_begin            ()                     override;
    void on_end              ()                     override;
    [[nodiscard]] auto want_keyboard_events() const -> bool       override;
    [[nodiscard]] auto want_mouse_events   () const -> bool       override;

    // Public API
    void set_map(Map* map);

    // Commands
    [[nodiscard]] auto mouse_scroll_try_ready() const -> bool;
    void scroll                (glm::vec2 delta);
    void scroll_tiles          (glm::vec2 delta);
    void grid_cycle            ();

    void scroll_to    (Tile_coordinate center_tile);
    void hover        (glm::vec2 window_position);
    void render       ();
    void blit         (Pixel_coordinate shape, Tile_coordinate tile_location, uint32_t color = 0xffffffff) const;
    void print        (std::string_view text, Tile_coordinate tile_location, uint32_t color = 0xffffffff) const;

    [[nodiscard]] auto wrap         (Tile_coordinate in) const -> Tile_coordinate;
    [[nodiscard]] auto wrap_center  (Tile_coordinate in) const -> Tile_coordinate;
    [[nodiscard]] bool hit_tile     (Tile_coordinate tile_coordinate) const;
    [[nodiscard]] auto pixel_to_tile(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate;
    void scale_zoom   (float scale);
    void set_zoom     (float scale);

    auto tile_image   (terrain_tile_t terrain, int scale = 1) -> bool;
    [[nodiscard]] auto tile_position(Tile_coordinate coordinate) const -> glm::vec2;

    [[nodiscard]] auto get_hover_window_position() const -> std::optional<glm::vec2>       { return m_hover_window_position; }
    [[nodiscard]] auto get_hover_tile_position  () const -> std::optional<Tile_coordinate> { return m_hover_tile_position; }

private:
    [[nodiscard]] auto normalize(Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate;

    // Commands
    erhe::renderer::Text_renderer& m_text_renderer;
    Tile_renderer&                 m_tile_renderer;

    std::optional<glm::vec2>       m_hover_window_position;
    std::optional<Tile_coordinate> m_hover_tile_position;

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
    int                           m_grid        {0}; // 2
    float                         m_zoom        {1.0f};
    glm::vec2                     m_pixel_offset{0.0f, 0.0f};
    Tile_coordinate               m_center_tile {0, 0};

    bool                          m_is_hovered{false};
};

} // namespace hextiles
