#pragma once

#include "coordinate.hpp"
#include "texture_util.hpp"
#include "types.hpp"
#include "tile_shape.hpp"
#include "unit_type.hpp"

#include "erhe/application/renderers/buffer_writer.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include <glm/glm.hpp>

#include <array>
#include <bitset>
#include <cstdint>
#include <deque>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics
{
    class Buffer;
    class OpenGL_state_tracker;
    class Sampler;
    class Shader_stages;
    class Texture;
}

namespace erhe::scene
{
    class Camera;
    class Viewport;
}

namespace erhe::ui
{
    class Font;
}

namespace hextiles
{

class Tiles;

//struct Player_unit
//{
//    int    player;
//    unit_t unit;
//};

class Tile_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Tile_renderer"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Tile_renderer  ();
    ~Tile_renderer () noexcept override;
    Tile_renderer  (const Tile_renderer&) = delete;
    void operator=(const Tile_renderer&)  = delete;
    Tile_renderer  (Tile_renderer&&)      = delete;
    void operator=(Tile_renderer&&)       = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    [[nodiscard]] auto tileset_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto tileset_image  () const -> const Image&;

    [[nodiscard]] auto get_multi_unit_tile  (std::array<int, Battle_type::bit_count> battle_type_players) const -> unit_tile_t;
    [[nodiscard]] auto get_single_unit_tile (int player, unit_t unit) const -> unit_tile_t;
    [[nodiscard]] auto get_special_unit_tile(int special_unit_tile_index) const -> unit_tile_t;

    [[nodiscard]] auto get_terrain_shape (const terrain_tile_t terrain) const -> Pixel_coordinate;
    [[nodiscard]] auto get_unit_shape    (const unit_tile_t unit) const -> Pixel_coordinate;
    [[nodiscard]] auto get_terrain_shapes() const -> const std::vector<Pixel_coordinate>&;
    [[nodiscard]] auto get_unit_shapes   () const -> const std::vector<Pixel_coordinate>&;
    [[nodiscard]] auto get_extra_shape   (int extra) const -> Pixel_coordinate;

    ///[[nodiscard]] auto get_terrain_tile(const terrain_t terrain  ) const -> tile_t;
    ///[[nodiscard]] auto get_unit_tile   (const unit_t    unit     ) const -> tile_t;
    ///[[nodiscard]] auto get_grid_tile   (const int       grid_mode) const -> tile_t;

    void blit(
        int      src_x,
        int      src_y,
        int      width,
        int      height,
        float    dst_x0,
        float    dst_y0,
        float    dst_x1,
        float    dst_y1,
        uint32_t color
    );

    //void blit_tile(
    //    tile_t   tile,
    //    float    dst_x0,
    //    float    dst_y0,
    //    float    dst_x1,
    //    float    dst_y1,
    //    uint32_t color
    //);

    void begin    ();
    void end      ();

    void render    (erhe::scene::Viewport viewport);
    void next_frame();

private:
    gsl::span<float>    m_gpu_float_data;
    gsl::span<uint32_t> m_gpu_uint_data;
    size_t              m_word_offset{0};
    bool                m_can_blit   {false};

    static constexpr size_t s_frame_resources_count = 4;

    class Frame_resources
    {
    public:
        static constexpr gl::Buffer_storage_mask storage_mask{
            gl::Buffer_storage_mask::map_coherent_bit   |
            gl::Buffer_storage_mask::map_persistent_bit |
            gl::Buffer_storage_mask::map_write_bit
        };

        static constexpr gl::Map_buffer_access_mask access_mask{
            gl::Map_buffer_access_mask::map_coherent_bit   |
            gl::Map_buffer_access_mask::map_persistent_bit |
            gl::Map_buffer_access_mask::map_write_bit
        };

        Frame_resources(
            const size_t                              vertex_count,
            erhe::graphics::Shader_stages*            shader_stages,
            erhe::graphics::Vertex_attribute_mappings attribute_mappings,
            erhe::graphics::Vertex_format&            vertex_format,
            erhe::graphics::Buffer*                   index_buffer,
            const size_t                              slot
        );

        Frame_resources(const Frame_resources&) = delete;
        auto operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&) = delete;
        auto operator= (Frame_resources&&) = delete;

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             projection_buffer;
        erhe::graphics::Vertex_input_state vertex_input;
        erhe::graphics::Pipeline           pipeline;
    };

    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;
    void create_frame_resources(size_t vertex_count);

    static constexpr size_t player_color_shade_count = 4;
    struct Player_unit_colors
    {
        std::array<glm::vec4, player_color_shade_count> shades;
    };
    //std::vector<Player_unit_colors> players_colors;

    void compose_tileset_texture();
    void compose_multiple_unit_tile
    (
        Image&                                  scratch,
        const std::vector<Player_unit_colors>&  players_colors,
        std::array<int, Battle_type::bit_count> players,
        int                                     y0
    );

    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Tiles>                                m_tiles;

    erhe::graphics::Fragment_outputs                 m_fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings        m_attribute_mappings;
    erhe::graphics::Vertex_format                    m_vertex_format;
    std::shared_ptr<erhe::graphics::Buffer>          m_index_buffer;
    std::unique_ptr<erhe::graphics::Shader_resource> m_projection_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   m_shader_stages;
    size_t                                           m_u_clip_from_window_size  {0};
    size_t                                           m_u_clip_from_window_offset{0};
    size_t                                           m_u_texture_size           {0};
    size_t                                           m_u_texture_offset         {0};
    std::unique_ptr<erhe::graphics::Sampler>         m_nearest_sampler;
    std::shared_ptr<erhe::graphics::Texture>         m_tileset_texture;
    Image                                            m_tileset_image;

    std::deque<Frame_resources>      m_frame_resources;
    size_t                           m_current_frame_resource_slot{0};

    erhe::application::Buffer_writer m_vertex_writer;
    erhe::application::Buffer_writer m_projection_writer;
    size_t                           m_index_range_first{0};
    size_t                           m_index_count      {0};

    // Tile layout:
    // - 8 * 7 : terrain group tiles 8 x 8, 7 groups
    // -     2 : extra tiles 8 x 2 (edge x 6, grid x 2, brush size x 4, extra x 4)
    // -     7 : base terrain tiles 8 x 7
    // - 2 * 2 : explosion tiles 4 x 2, double width and height
    // -     2 : multiplayer tiles 6 x 2
    // - unit tiles 8 x 8

    //tile_t m_terrain_tile_offset   {0};
    //tile_t m_extra_tile_offset     {0};
    //tile_t m_unit_tile_offset      {0};
    //tile_t m_edge_tile_offset      {0};
    //tile_t m_grid_tile_offset      {0};
    //tile_t m_brush_size_tile_offset{0};
    std::vector<Pixel_coordinate>         m_terrain_shapes;
    std::vector<Pixel_coordinate>         m_multiplayer_shapes;
    std::vector<Pixel_coordinate>         m_unit_shapes;
    std::vector<Pixel_coordinate>         m_extra_shapes;
    std::vector<Pixel_coordinate>         m_explosion_shapes;

    static constexpr int s_special_unit_tile_count  = 8;
    static constexpr int s_single_unit_tile_count   = max_player_count * Unit_group::width * Unit_group::height;
    static constexpr int s_multiple_unit_tile_count =
        (max_player_count + 1) * // air
        (max_player_count + 1) * // ground
        (max_player_count + 1) * // sea
        (max_player_count + 1);  // underwater
    static constexpr int s_special_unit_tile_offset  = 0;
    static constexpr int s_single_unit_tile_offset   = s_special_unit_tile_count;
    static constexpr int s_multiple_unit_tile_offset = s_single_unit_tile_offset + s_single_unit_tile_count;
    static constexpr int s_unit_tile_count           = s_multiple_unit_tile_count;
    static constexpr int s_unit_tile_height = (s_unit_tile_count + 7) / 8;
    static constexpr std::array<int, Battle_type::bit_count> s_multiple_unit_battle_type_multiplier
    {
        (max_player_count + 1) * (max_player_count + 1) * (max_player_count + 1), // air
        (max_player_count + 1) * (max_player_count + 1), // ground
        (max_player_count + 1), // sea
        1, // underwater
        0  // city
    };

};

} // namespace hextiles
