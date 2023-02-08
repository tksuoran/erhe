#pragma once

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "tile_shape.hpp"
#include "types.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/windows/framebuffer_window.hpp"

//#include <array>
#include <numeric>
//#include <string>
//#include <vector>


namespace hextiles
{

class Map;
class Pixel_lookup;

class Map_scroll_command final
    : public erhe::application::Command
{
public:
    Map_scroll_command(float dx, float dy);
    auto try_call() -> bool override;

private:
    glm::vec2 m_offset;
};

class Map_free_zoom_command final
    : public erhe::application::Command
{
public:
    Map_free_zoom_command();
    auto try_call_with_input(erhe::application::Input_arguments& input) -> bool override;
};

class Map_mouse_scroll_command final
    : public erhe::application::Command
{
public:
    Map_mouse_scroll_command();
    void try_ready          () override;
    auto try_call_with_input(erhe::application::Input_arguments& input) -> bool override;
};

class Map_zoom_command
    : public erhe::application::Command
{
public:
    explicit Map_zoom_command(float scale);
    auto try_call() -> bool override;

private:
    float m_scale{1.0f};
};

class Map_grid_cycle_command
    : public erhe::application::Command
{
public:
    Map_grid_cycle_command();
    auto try_call() -> bool override;
};

class Map_window
    : public erhe::components::Component
    , public erhe::application::Framebuffer_window
{
public:
    static constexpr std::string_view c_type_name{"Map_window"};
    static constexpr std::string_view c_title{"Map"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Map_window ();
    ~Map_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Framebuffer_window
    auto get_size(glm::vec2 available_size) const -> glm::vec2 override;

    // Overrides Imgui_window
    auto flags               () -> ImGuiWindowFlags override;
    void on_begin            () override;
    void on_end              () override;
    auto want_keyboard_events() const -> bool override;
    auto want_mouse_events   () const -> bool override;

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
    std::unique_ptr<Pixel_lookup> m_pixel_lookup;
    int                           m_grid        {2};
    float                         m_zoom        {1.0f};
    glm::vec2                     m_pixel_offset{0.0f, 0.0f};
    Tile_coordinate               m_center_tile {0, 0};
};

extern Map_window* g_map_window;

} // namespace hextiles
