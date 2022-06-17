#pragma once

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "tile_shape.hpp"
#include "types.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/framebuffer_window.hpp"

#include "erhe/components/components.hpp"

#include <array>
#include <numeric>
#include <string>
#include <vector>

namespace erhe::application
{
    class Imgui_renderer;
    class Text_renderer;
    class View;
}

namespace hextiles
{

class Map;
class Tile_renderer;
class Map_window;
class Pixel_lookup;
class Tiles;


class Map_scroll_command final
    : public erhe::application::Command
{
public:
    Map_scroll_command(
        Map_window& map_window,
        const float dx,
        const float dy
    )
        : Command     {"Map.scroll"}
        , m_map_window{map_window}
        , m_offset    {dx, dy}
    {
    }
    ~Map_scroll_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
    glm::vec2   m_offset;
};

class Map_free_zoom_command final
    : public erhe::application::Command
{
public:
    explicit Map_free_zoom_command(Map_window& map_window)
        : Command     {"Map.hover"}
        , m_map_window{map_window}
    {
    }
    ~Map_free_zoom_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
};

class Map_mouse_scroll_command final
    : public erhe::application::Command
{
public:
    explicit Map_mouse_scroll_command(Map_window& map_window)
        : Command     {"Map.mouse_scroll"}
        , m_map_window{map_window}
    {
    }
    ~Map_mouse_scroll_command() noexcept final {}

    auto try_call (erhe::application::Command_context& context) -> bool override;
    void try_ready(erhe::application::Command_context& context) override;

private:
    Map_window& m_map_window;
};

class Map_zoom_command
    : public erhe::application::Command
{
public:
    Map_zoom_command(
        Map_window& map_window,
        const float scale
    )
        : Command     {"Map.scroll"}
        , m_map_window{map_window}
        , m_scale     {scale}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
    float       m_scale;
};

class Map_grid_cycle_command
    : public erhe::application::Command
{
public:
    explicit Map_grid_cycle_command(
        Map_window& map_window
    )
        : Command     {"Map.grid_cycle"}
        , m_map_window{map_window}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
};

class Map_window
    : public erhe::components::Component
    , public erhe::application::Framebuffer_window
{
public:
    static constexpr std::string_view c_label{"Map_window"};
    static constexpr std::string_view c_title{"Map"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Map_window ();
    ~Map_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Framebuffer_window
    auto get_size(glm::vec2 available_size) const -> glm::vec2 override;

    // Overrides Imgui_window
    auto flags               () -> ImGuiWindowFlags override;
    auto consumes_mouse_input() const -> bool override;
    void on_begin            () override;
    void on_end              () override;

    // Public API
    void set_map(const std::shared_ptr<Map>& map);
    //auto get_map              () const -> const std::shared_ptr<Map>&;

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

    // Component dependencies
    std::shared_ptr<Map>                              m_map;
    std::shared_ptr<Tile_renderer>                    m_tile_renderer;
    std::shared_ptr<erhe::application::Text_renderer> m_text_renderer;
    std::shared_ptr<Tiles>                            m_tiles;
    std::shared_ptr<erhe::application::View>          m_editor_view;

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

    std::unique_ptr<Pixel_lookup> m_pixel_lookup;
    int                           m_grid        {2};
    float                         m_zoom        {1.0f};
    glm::vec2                     m_pixel_offset{0.0f, 0.0f};
    Tile_coordinate               m_center_tile {0, 0};
};

} // namespace hextiles
