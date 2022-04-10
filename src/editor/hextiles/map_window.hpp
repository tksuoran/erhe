#pragma once

#include "hextiles/coordinate.hpp"
#include "hextiles/terrain_type.hpp"
#include "hextiles/tile_shape.hpp"

#include "commands/command.hpp"

#include "windows/framebuffer_window.hpp"

#include "erhe/components/components.hpp"

#include <string>
#include <vector>

namespace editor
{
class Editor_view;
class Text_renderer;
}

namespace hextiles
{

class Map;
class Map_renderer;
class Map_window;
class Pixel_lookup;
class editor::Pointer_context;
class Tiles;

class Map_scroll_command
    : public editor::Command
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

    auto try_call(editor::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
    glm::vec2   m_offset;
};

class Map_free_zoom_command
    : public editor::Command
{
public:
    Map_free_zoom_command(Map_window& map_window)
        : Command     {"Map.hover"}
        , m_map_window{map_window}
    {
    }
    ~Map_free_zoom_command() noexcept final {}

    auto try_call(editor::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
};

class Map_hover_command
    : public editor::Command
{
public:
    Map_hover_command(Map_window& map_window)
        : Command     {"Map.hover"}
        , m_map_window{map_window}
    {
    }
    ~Map_hover_command() noexcept final {}

    auto try_call(editor::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
};

class Map_mouse_scroll_command
    : public editor::Command
{
public:
    Map_mouse_scroll_command(Map_window& map_window)
        : Command     {"Map.mouse_scroll"}
        , m_map_window{map_window}
    {
    }
    ~Map_mouse_scroll_command() noexcept final {}

    auto try_call (editor::Command_context& context) -> bool override;
    void try_ready(editor::Command_context& context) override;

private:
    Map_window& m_map_window;
};

class Map_zoom_command
    : public editor::Command
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
    //~Map_zoom_command() noexcept final {}

    auto try_call(editor::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
    float       m_scale;
};

class Map_primary_brush_command
    : public editor::Command
{
public:
    Map_primary_brush_command(Map_window& map_window)
        : Command     {"Map.primary_brush"}
        , m_map_window{map_window}
    {
    }
    ~Map_primary_brush_command() noexcept final {}

    auto try_call (editor::Command_context& context) -> bool override;
    void try_ready(editor::Command_context& context) override;

private:
    Map_window& m_map_window;
};

class Terrain_palette_window
    : public erhe::components::Component
    , public editor::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Terrain_palette_window"};
    static constexpr std::string_view c_title{"Terrain palette"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Terrain_palette_window ();
    ~Terrain_palette_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Map_window> m_map_window;
};

class Map_tool_window
    : public erhe::components::Component
    , public editor::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Map_tool"};
    static constexpr std::string_view c_title{"Map Tool"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Map_tool_window ();
    ~Map_tool_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Map_window> m_map_window;
};

class Map_window
    : public erhe::components::Component
    , public editor::Framebuffer_window
{
public:
    static constexpr std::string_view c_name {"Map_window"};
    static constexpr std::string_view c_title{"Map"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Map_window ();
    ~Map_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Framebuffer_window
    auto get_size(glm::vec2 available_size) const -> glm::vec2 override;

    // Overrides Imgui_window
    auto flags               () -> ImGuiWindowFlags override;
    auto consumes_mouse_input() const -> bool override;
    void on_begin            () override;
    void on_end              () override;

    // Public API
    void tool_imgui           ();
    void terrain_palette_imgui();

    // Commands
    void hover                 (glm::vec2 window_position);
    auto mouse_scroll_try_ready() const -> bool;
    void scroll                (glm::vec2 delta);
    void scroll_tiles          (glm::vec2 delta);
    void primary_brush         (glm::vec2 mouse_position);

    void render       ();
    auto wrap         (Tile_coordinate in) const -> Tile_coordinate;
    auto wrap_center  (Tile_coordinate in) const -> Tile_coordinate;
    bool hit_tile     (Tile_coordinate tile_coordinate) const;
    auto pixel_to_tile(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate;
    void scale_zoom   (float scale);
    void set_zoom     (float scale);

private:
    auto normalize      (Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate;
    auto tile_image     (terrain_t terrain, const int scale = 1) -> bool;
    void tile_info      (const Tile_coordinate tile_position);
    void terrain_palette();
    auto tile_position  (const Tile_coordinate coordinate) const -> glm::vec2;

    // Component dependencies
    std::shared_ptr<Map>                     m_map;
    std::shared_ptr<Map_renderer>            m_map_renderer;
    std::shared_ptr<editor::Text_renderer>   m_text_renderer;
    std::shared_ptr<Tiles>                   m_tiles;
    std::shared_ptr<editor::Editor_view>     m_editor_view;
    std::shared_ptr<editor::Pointer_context> m_pointer_context;

    std::unique_ptr<Pixel_lookup>            m_pixel_lookup;

    // Commands
    Map_free_zoom_command     m_free_zoom_command;
    Map_mouse_scroll_command  m_mouse_scroll_command;
    Map_hover_command         m_map_hover_command;
    Map_primary_brush_command m_map_primary_brush_command;

    Map_scroll_command  m_scroll_left_command;
    Map_scroll_command  m_scroll_right_command;
    Map_scroll_command  m_scroll_up_command;
    Map_scroll_command  m_scroll_down_command;

    Map_zoom_command    m_zoom_in_command;
    Map_zoom_command    m_zoom_out_command;

    int                 m_grid{2};
    float               m_zoom{1.0f};

    int                 m_brush_size{1};
    terrain_t           m_left_brush{Terrain_default};

    std::optional<glm::vec2>        m_hover_window_position;
    std::optional<Tile_coordinate>  m_hover_tile_position;

    // view cache - updated by update_view()
    float           m_scaled_tile_width {Tile_shape::interleave_width};
    float           m_scaled_tile_height{Tile_shape::height          };
    glm::vec2       m_pixel_offset      {0.0f, 0.0f};
    Tile_coordinate m_center_tile       {0, 0};  // LX, LY
    //m_center_tile; // LX, LY

};

}
