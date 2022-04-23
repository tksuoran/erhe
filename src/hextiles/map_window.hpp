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
class Map_renderer;
class Map_window;
class Pixel_lookup;
class Tiles;

class Fbm_noise
{
public:
    void prepare ();
    auto generate(float s, float t, glm::vec4 seed) -> float;
    void imgui();

private:
    auto generate(float x, float y, float z, float w, glm::vec4 seed) -> float;

    float m_bounding   {0.0f};
    float m_frequency  {1.0f};
    float m_lacunarity {1.70f};
    float m_gain       {0.85f};
    int   m_octaves    {10};
    float m_scale[2]   {0.1f, 0.1f};
    float m_location[2]{42.0f, 42.0f};
};

struct Terrain_variation
{
    int       id               {0};
    terrain_t base_terrain     {0};
    terrain_t variation_terrain{0};
    float     ratio            {1.0f};
    float     normalized_ratio {1.0f};
    float     threshold        {0.0f};
};

struct Variations
{
    void reset                   (size_t count);
    void push                    (float value);
    auto get_noise_value         (size_t index) const -> float;
    auto normalize               ();
    void compute_threshold_values();
    auto get                     (size_t index) const -> Terrain_variation;
    auto make                    (int id, terrain_t base_terrain, terrain_t variation_terrain = 0) const -> Terrain_variation;
    void assign                  (std::vector<Terrain_variation>&& terrains);

    float                          m_min_value;
    float                          m_max_value;
    std::vector<float>             m_values;
    std::vector<Terrain_variation> m_terrains;
};

class Map_scroll_command
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

class Map_free_zoom_command
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

class Map_hover_command
    : public erhe::application::Command
{
public:
    explicit Map_hover_command(Map_window& map_window)
        : Command     {"Map.hover"}
        , m_map_window{map_window}
    {
    }
    ~Map_hover_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
};

class Map_mouse_scroll_command
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
    explicit Map_zoom_command(
        Map_window& map_window,
        const float scale
    )
        : Command     {"Map.scroll"}
        , m_map_window{map_window}
        , m_scale     {scale}
    {
    }
    //~Map_zoom_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Map_window& m_map_window;
    float       m_scale;
};

class Map_primary_brush_command
    : public erhe::application::Command
{
public:
    explicit Map_primary_brush_command(Map_window& map_window)
        : Command     {"Map.primary_brush"}
        , m_map_window{map_window}
    {
    }
    ~Map_primary_brush_command() noexcept final {}

    auto try_call (erhe::application::Command_context& context) -> bool override;
    void try_ready(erhe::application::Command_context& context) override;

private:
    Map_window& m_map_window;
};

class Terrain_palette_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
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
    , public erhe::application::Imgui_window
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
    std::shared_ptr<Map_window>                       m_map_window;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
};

class Map_window
    : public erhe::components::Component
    , public erhe::application::Framebuffer_window
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
    auto get_map              () const -> const std::shared_ptr<Map>&;

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

    void update_elevation_terrains();

private:
    auto normalize      (Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate;
    auto tile_image     (terrain_t terrain, const int scale = 1) -> bool;
    void tile_info      (const Tile_coordinate tile_position);
    void terrain_palette();
    auto tile_position  (const Tile_coordinate coordinate) const -> glm::vec2;

    void update_group_terrain             (Tile_coordinate position);
    void generate_noise_pass              ();
    void generate_compute_threshold_values();
    void generate_base_terrain_pass       ();
    auto get_variation                    (terrain_t base_terrain, float temperature, float humidity, float variation) const -> terrain_t;
    void generate_variation_pass          ();
    void apply_rule                       (const Terrain_replacement_rule& rule);
    void generate_apply_rules_pass        ();
    void generate_group_fix_pass          ();

    // Component dependencies
    std::shared_ptr<Map>                              m_map;
    std::shared_ptr<Map_renderer>                     m_map_renderer;
    std::shared_ptr<erhe::application::Text_renderer> m_text_renderer;
    std::shared_ptr<Tiles>                            m_tiles;
    std::shared_ptr<erhe::application::View>          m_editor_view;

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

    glm::vec2       m_pixel_offset{0.0f, 0.0f};
    Tile_coordinate m_center_tile {0, 0};  // LX, LY

    Fbm_noise   m_noise;

    Variations  m_elevation_generator;
    Variations  m_temperature_generator;
    Variations  m_humidity_generator;
    Variations  m_variation_generator;

    struct Biome
    {
        terrain_t base_terrain;
        terrain_t variation;
        int       priority;
        float     min_temperature;
        float     max_temperature;
        float     min_humidity;
        float     max_humidity;
        float     ratio;
    };
    std::vector<Biome> m_biomes;
};

} // namespace hextiles
