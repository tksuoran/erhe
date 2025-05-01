#pragma once

#include "coordinate.hpp"
#include "texture_util.hpp"
#include "types.hpp"
#include "tile_shape.hpp"
#include "unit_type.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_renderer/gpu_ring_buffer.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics{
    class Buffer;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
    class Texture;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace hextiles {

class Tiles;

class Tile_renderer
{
public:
    Tile_renderer(
        erhe::graphics::Instance&    graphics_instance,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        Tiles&                       tiles
    );

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

    auto terrain_image(const terrain_tile_t terrain_tile, const int scale) -> bool;
    auto unit_image   (const unit_tile_t    unit_tile,    const int scale) -> bool;
    void show_texture ();
    void make_terrain_type_combo(const char* label, terrain_t& value);
    void make_unit_type_combo   (const char* label, unit_t& value, const int player = 0);

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

    void begin    ();
    void end      ();

    void render    (erhe::math::Viewport viewport);

private:
    auto make_prototype(erhe::graphics::Instance& graphics_instance) const -> erhe::graphics::Shader_stages_prototype;
    auto make_program(erhe::graphics::Shader_stages_prototype&& prototype) const -> erhe::graphics::Shader_stages;

    erhe::graphics::Instance&    m_graphics_instance;
    erhe::imgui::Imgui_renderer& m_imgui_renderer;
    Tiles&                       m_tiles;

    std::span<float>    m_gpu_float_data;
    std::span<uint32_t> m_gpu_uint_data;
    size_t              m_word_offset{0};
    bool                m_can_blit   {false};

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

    erhe::graphics::Shader_resource           m_default_uniform_block; // containing sampler uniforms for non-bindless textures
    erhe::graphics::Shader_resource*          m_texture_sampler;       // non-bindless
    erhe::graphics::Fragment_outputs          m_fragment_outputs;
    erhe::dataformat::Vertex_format           m_vertex_format;
    erhe::graphics::Buffer                    m_index_buffer;
    erhe::graphics::Sampler                   m_nearest_sampler;
    erhe::graphics::Shader_resource           m_projection_block;
    erhe::graphics::Shader_resource*          m_clip_from_window;
    erhe::graphics::Shader_resource*          m_texture_handle;

    size_t                                    m_u_clip_from_window_size  {0};
    size_t                                    m_u_clip_from_window_offset{0};
    size_t                                    m_u_texture_size           {0};
    size_t                                    m_u_texture_offset         {0};
    std::filesystem::path                     m_shader_path;
    erhe::graphics::Shader_stages             m_shader_stages;
    std::shared_ptr<erhe::graphics::Texture>  m_tileset_texture;
    Image                                     m_tileset_image;

    erhe::renderer::GPU_ring_buffer           m_vertex_buffer;
    erhe::renderer::GPU_ring_buffer           m_projection_buffer;
    erhe::graphics::Vertex_input_state        m_vertex_input;
    erhe::graphics::Pipeline                  m_pipeline;

    std::optional<erhe::renderer::Buffer_range> m_vertex_buffer_range;
    size_t                                      m_vertex_write_offset{0};
    size_t                                      m_index_count        {0};

    // Tile layout:
    // - 8 * 7 : terrain group tiles 8 x 8, 7 groups
    // -     2 : extra tiles 8 x 2 (edge x 6, grid x 2, brush size x 4, extra x 4)
    // -     7 : base terrain tiles 8 x 7
    // - 2 * 2 : explosion tiles 4 x 2, double width and height
    // -     2 : multiplayer tiles 6 x 2
    // - unit tiles 8 x 8
    std::vector<Pixel_coordinate> m_terrain_shapes;
    std::vector<Pixel_coordinate> m_multiplayer_shapes;
    std::vector<Pixel_coordinate> m_unit_shapes;
    std::vector<Pixel_coordinate> m_extra_shapes;
    std::vector<Pixel_coordinate> m_explosion_shapes;

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
