#include "tile_renderer.hpp"

#include "tiles.hpp"
#include "hextiles.hpp"
#include "hextiles_log.hpp"

#include "erhe_graphics/gl_context_provider.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/scoped_buffer_mapping.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdarg>

namespace hextiles
{

namespace {

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

}

using erhe::graphics::Shader_stages;

Tile_renderer::Frame_resources::Frame_resources(
    erhe::graphics::Instance&                 graphics_instance,
    const size_t                              vertex_count,
    erhe::graphics::Shader_stages&            shader_stages,
    erhe::graphics::Vertex_attribute_mappings attribute_mappings,
    erhe::graphics::Vertex_format&            vertex_format,
    erhe::graphics::Buffer&                   index_buffer,
    const size_t                              slot
)
    : vertex_buffer{
        graphics_instance,
        gl::Buffer_target::array_buffer,
        vertex_format.stride() * vertex_count,
        storage_mask,
        access_mask
    }
    , projection_buffer{
        graphics_instance,
        gl::Buffer_target::uniform_buffer,
        1024, // TODO
        storage_mask,
        access_mask
    }
    , vertex_input{
        erhe::graphics::Vertex_input_state_data::make(
            attribute_mappings,
            vertex_format,
            &vertex_buffer,
            &index_buffer
        )
    }
    , pipeline{
        erhe::graphics::Pipeline_data{
            .name           = "Map renderer",
            .shader_stages  = &shader_stages,
            .vertex_input   = &vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied,
        }
    }
{
    vertex_buffer    .set_debug_label(fmt::format("Map Renderer Vertex {}", slot));
    projection_buffer.set_debug_label(fmt::format("Map Renderer Projection {}", slot));
}

constexpr size_t uint32_primitive_restart{0xffffffffu};
constexpr size_t per_quad_vertex_count   {4}; // corner count
constexpr size_t per_quad_index_count    {per_quad_vertex_count + 1}; // Plus one for primitive restart
constexpr size_t max_quad_count          {1'000'000}; // each quad consumes 4 indices
constexpr size_t index_count             {max_quad_count * per_quad_index_count};
constexpr size_t index_stride            {4};

auto Tile_renderer::make_prototype(erhe::graphics::Instance& graphics_instance) const
-> erhe::graphics::Shader_stages_prototype
{
    erhe::graphics::Shader_stages_create_info create_info{
        .name                      = "tile",
        .interface_blocks          = { &m_projection_block },
        .vertex_attribute_mappings = &m_attribute_mappings,
        .fragment_outputs          = &m_fragment_outputs,
        .default_uniform_block     = m_graphics_instance.info.use_bindless_texture
            ? nullptr
            : &m_default_uniform_block,
        .shaders = {
            { igl::ShaderStage::vertex_shader,   m_shader_path / std::filesystem::path{"tile.vert"} },
            { igl::ShaderStage::fragment_shader, m_shader_path / std::filesystem::path{"tile.frag"} }
        },
        .dump_interface    = true,
        .dump_final_source = true
    };

    if (m_graphics_instance.info.gl_version < 430) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object));
        create_info.extensions.push_back({igl::ShaderStage::vertex_shader,   "GL_ARB_shader_storage_buffer_object"});
        create_info.extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_shader_storage_buffer_object"});
    }
    if (m_graphics_instance.info.gl_version < 460) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_draw_parameters));
        create_info.extensions.push_back({igl::ShaderStage::vertex_shader,   "GL_ARB_shader_draw_parameters"});
        create_info.defines.push_back({"gl_DrawID", "gl_DrawIDARB"});
    }

    if (m_graphics_instance.info.use_bindless_texture) {
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        create_info.extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_bindless_texture"});
    }

    return erhe::graphics::Shader_stages_prototype{graphics_instance, create_info};
}

auto Tile_renderer::make_program(
    erhe::graphics::Shader_stages_prototype&& prototype
) const -> erhe::graphics::Shader_stages
{
    if (!prototype.is_valid()) {
        log_startup->error("current directory is {}", std::filesystem::current_path().string());
        log_startup->error("Compiling shader program {} failed", prototype.name());
        return erhe::graphics::Shader_stages{prototype.name()};
    }

    return erhe::graphics::Shader_stages{std::move(prototype)};
}

Tile_renderer::Tile_renderer(
    erhe::graphics::Instance&    graphics_instance,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    Tiles&                       tiles
)
    : m_graphics_instance    {graphics_instance}
    , m_imgui_renderer       {imgui_renderer}
    , m_tiles                {tiles}
    , m_default_uniform_block{graphics_instance}
    , m_texture_sampler{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : m_default_uniform_block.add_sampler("s_texture", igl::UniformType::sampler_2d, 0)
    }
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , m_attribute_mappings{
        graphics_instance,
        {
            erhe::graphics::Vertex_attribute_mapping::a_position_float_vec2(),
            erhe::graphics::Vertex_attribute_mapping::a_color_float_vec4(),
            erhe::graphics::Vertex_attribute_mapping::a_texcoord_float_vec2(),
        }
    }
    , m_vertex_format{
        erhe::graphics::Vertex_attribute::position_float2 (),
        erhe::graphics::Vertex_attribute::color_ubyte4    (),
        erhe::graphics::Vertex_attribute::texcoord0_float2()
    }
    , m_index_buffer{
        graphics_instance,
        gl::Buffer_target::element_array_buffer,
        index_stride * index_count,
        gl::Buffer_storage_mask::map_write_bit
    }
    , m_nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Tile_renderer"
        }
    }
    , m_projection_block{
        graphics_instance,
        "projection",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_clip_from_window         {m_projection_block.add_mat4 ("clip_from_window")}
    , m_texture_handle           {m_projection_block.add_uvec2("texture")}
    , m_u_clip_from_window_size  {m_clip_from_window->size_bytes()}
    , m_u_clip_from_window_offset{m_clip_from_window->offset_in_parent()}
    , m_u_texture_size           {m_texture_handle->size_bytes()}
    , m_u_texture_offset         {m_texture_handle->offset_in_parent()}
    , m_shader_path              {std::filesystem::path{"res"} / std::filesystem::path{"shaders"}}
    , m_shader_stages            {make_program(make_prototype(graphics_instance))}
    , m_vertex_writer            {graphics_instance}
    , m_projection_writer        {graphics_instance}
        
{
    erhe::graphics::Scoped_buffer_mapping<uint32_t> index_buffer_map{
        m_index_buffer,
        0,
        index_count,
        gl::Map_buffer_access_mask::map_write_bit
    };

    const auto& gpu_index_data = index_buffer_map.span();
    size_t      offset      {0};
    uint32_t    vertex_index{0};
    for (unsigned int i = 0; i < max_quad_count; ++i)
    {
        gpu_index_data[offset + 0] = vertex_index;
        gpu_index_data[offset + 1] = vertex_index + 1;
        gpu_index_data[offset + 2] = vertex_index + 2;
        gpu_index_data[offset + 3] = vertex_index + 3;
        gpu_index_data[offset + 4] = uint32_primitive_restart;
        vertex_index += 4;
        offset += 5;
    }

    create_frame_resources(max_quad_count * per_quad_vertex_count);

    compose_tileset_texture();
}

static constexpr std::string_view c_text_renderer_initialize_component{"Tile_renderer::initialize_component()"};

void Tile_renderer::compose_tileset_texture()
{
    const auto texture_path =
        std::filesystem::path("res") /
        std::filesystem::path("hextiles") /
        std::filesystem::path("hextiles.png");
    m_tileset_image = load_png(texture_path);

    // Tile layout:
    // -     7 : base terrain tiles 8 x 7
    // - 8 * 7 : terrain group tiles 8 x 8, 7 groups
    // -     1 : extra tiles 8 x 2 (grid x 2, brush size x 4, extra x 2)
    // - 2 * 2 : explosion tiles 4 x 2, double width and height
    // -     1 : special unit tiles 8 x 1
    // -       : unit tiles 8 x 7

    int ty_offset{0}; // in tiles

    // Basetiles
    //int ty0_base_terrain = ty_offset;
    for (int ty = 0; ty < Base_tiles::height; ++ty) {
        for (int tx = 0; tx < Base_tiles::width; ++tx) {
            m_terrain_shapes.emplace_back(
                tx * Tile_shape::full_width,
                (ty + ty_offset) * Tile_shape::height
            );
        }
    }
    ty_offset += Base_tiles::height;

    // Terrain groups
    //int ty0_terrain_groups = ty_offset;
    for (int ty = 0; ty < Tile_group::count * Tile_group::height; ++ty) {
        for (int tx = 0; tx < Tile_group::width; ++tx) {
            m_terrain_shapes.emplace_back(
                tx * Tile_shape::full_width,
                (ty + ty_offset) * Tile_shape::height
            );
        }
    }
    ty_offset += Tile_group::count * Tile_group::height;

    // Extra tiles
    //const int ty0_extra_tiles = ty_offset;
    for (int ty = 0; ty < Extra_tiles::height; ++ty) {
        for (int tx = 0; tx < Extra_tiles::width; ++tx) {
            m_extra_shapes.emplace_back(
                tx * Tile_shape::full_width,
                (ty + ty_offset) * Tile_shape::height
            );
        }
    }
    ty_offset += Extra_tiles::height;

    // Explosions
    //const int ty0_explosion = ty_offset;
    for (int ty = 0; ty < Explosion_tiles::height; ++ty) {
        for (int tx = 0; tx < Base_tiles::width; ++tx) {
            m_explosion_shapes.emplace_back(
                (tx * 2) * Tile_shape::full_width,
                (ty * 2 + ty_offset) * Tile_shape::height
            );
        }
    }
    ty_offset += Explosion_tiles::height * 2;

    // Y offsets in tiles
    const int ty0_special_unit_tiles = ty_offset;
    for (int ty = 0; ty < 1; ++ty) {
        for (int tx = 0; tx < Base_tiles::width; ++tx) {
            m_unit_shapes.emplace_back(
                tx * Tile_shape::full_width,
                (ty + ty_offset) * Tile_shape::height
            );
        }
    }
    ty_offset += 1;

    const int ty0_single_unit_tiles = ty_offset;
    for (int player = 0; player < max_player_count; ++player) {
        for (int ty = 0; ty < Unit_group::height; ++ty) {
            for (int tx = 0; tx < Unit_group::width; ++tx) {
                m_unit_shapes.emplace_back(
                    tx * Tile_shape::full_width,
                    (ty + ty_offset) * Tile_shape::height
                );
            }
        }
        ty_offset += Unit_group::height;
    }

    const int ty0_multiple_unit_tiles = ty_offset;
    {
        int players[4] = {0, 1, 2, 3};
        int tx = 0;
        int ty = ty_offset;

        for (players[0] = 0; players[0] < max_player_count + 1; ++players[0]) {
            for (players[1] = 0; players[1] < max_player_count + 1; ++players[1]) {
                for (players[2] = 0; players[2] < max_player_count + 1; ++players[2]) {
                    for (players[3] = 0; players[3] < max_player_count + 1; ++players[3]) {
                        m_unit_shapes.emplace_back(
                            tx * Tile_shape::full_width,
                            ty * Tile_shape::height
                        );

                        ++tx;
                        if (tx == 8) {
                            tx = 0;
                            ++ty;
                        }
                    }
                }
            }
        }
        ty_offset = ty + ((tx != 0) ? 1 : 0);
    }

    // Texture will be created with additional per-player colored unit tiles
    erhe::graphics::Texture_create_info texture_create_info{
        .instance        = m_graphics_instance,
        .target          = gl::Texture_target::texture_2d,
        .internal_format = to_gl(m_tileset_image.info.format),
        .use_mipmaps     = false,
        .width           = m_tileset_image.info.width,
        .height          = ty_offset * Tile_shape::height,
        .depth           = 1,
        .level_count     = 1
    };

    m_tileset_texture = std::make_shared<erhe::graphics::Texture>(texture_create_info);
    m_tileset_texture->set_debug_label(texture_path.string());
    float clear_rgba[4] = { 1.0f, 0.0f, 1.0f, 1.0f};
    if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
        gl::clear_tex_image(m_tileset_texture->gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &clear_rgba);
    } else {
        // TODO
    }

    // Upload everything before single unit tiles
    m_tileset_texture->upload_subimage(
        to_gl(m_tileset_image.info.format),
        m_tileset_image.data,
        m_tileset_image.info.width,
        0,
        0,
        m_tileset_image.info.width,
        ty0_single_unit_tiles * Tile_shape::height,
        0,    // mipmap level
        0,    // x
        0     // y
    );

    // Scan player colors
    std::vector<Player_unit_colors> players_colors;
    {
        int x0 = 0;
        int y0 = ty0_special_unit_tiles * Tile_shape::height;
        for (int i = 0; i < max_player_count; ++i) {
            Player_unit_colors player_colors;
            for (size_t j = 0; j < player_color_shade_count; ++j) {
                const glm::vec4 color = m_tileset_image.get_pixel(x0 + j, y0 + i);
                player_colors.shades[j] = color;
                log_tiles->info(
                    "Player {} Shade {} Color: {}, {}, {}, {}",
                    i, j,
                    color.r, color.g, color.b, color.a
                );
            }
            players_colors.push_back(player_colors);
        }
    }

    // Create per player colored variants of unit tiles
    {
        Image scratch{
            .info = {
                .width       = Unit_group::width * Tile_shape::full_width,
                .height      = Unit_group::height * Tile_shape::height,
                .depth       = 1,
                .level_count = 1,
                .row_stride  = 4 * Unit_group::width * Tile_shape::full_width,
                .format      = m_tileset_image.info.format
            },
            .data = {}
        };
        scratch.data.resize(scratch.info.height * scratch.info.row_stride);

        int y0 = ty0_single_unit_tiles * Tile_shape::height;
        for (int i = 0; i < max_player_count; ++i) {
            const Player_unit_colors& player_colors = players_colors[i];
            for (int y = 0; y < scratch.info.height; ++y) {
                for (int x = 0; x < scratch.info.width; ++x) {
                    const auto      original_color = m_tileset_image.get_pixel(x, y0 + y);
                    const glm::vec4 player_color   =
                        original_color.b * player_colors.shades[1] +
                        original_color.r * player_colors.shades[2] +
                        original_color.g * player_colors.shades[3];
                    scratch.put_pixel(x, y, player_color);
                }
            }
            m_tileset_texture->upload(
                to_gl(scratch.info.format),
                scratch.data,
                scratch.info.width,
                scratch.info.height,
                1,    // depth
                0,    // mipmap level
                0,    // x
                (ty0_single_unit_tiles + i * Unit_group::height) * Tile_shape::height // y
            );
        }
    }

    // Create colored variants of multiple unit tiles
    {
        std::array<int, Battle_type::bit_count> players;
        players.fill(0);
        Image scratch {
            .info = {
                .width       = Tile_shape::full_width,
                .height      = Tile_shape::height,
                .depth       = 1,
                .level_count = 1,
                .row_stride  = 4 * Tile_shape::full_width,
                .format      = m_tileset_image.info.format
            },
            .data = {}
        };
        scratch.data.resize(scratch.info.height * scratch.info.row_stride);
        int tx = 0;
        int ty = ty0_multiple_unit_tiles;
        for (players[0] = 0; players[0] < max_player_count + 1; ++players[0]) {
            for (players[1] = 0; players[1] < max_player_count + 1; ++players[1]) {
                for (players[2] = 0; players[2] < max_player_count + 1; ++players[2]) {
                    for (players[3] = 0; players[3] < max_player_count + 1; ++players[3]) {
                        compose_multiple_unit_tile(
                            scratch,
                            players_colors,
                            players,
                            (ty0_single_unit_tiles + 6) * Tile_shape::height
                        );
                        m_tileset_texture->upload(
                            to_gl(scratch.info.format),  // internal format
                            scratch.data,                // data span
                            scratch.info.width,          // width
                            scratch.info.height,         // height
                            1,                           // depth
                            0,                           // mipmap level
                            tx * Tile_shape::full_width, // destination x
                            ty * Tile_shape::height      // destination y
                        );

                        ++tx;
                        if (tx == 8) {
                            tx = 0;
                            ++ty;
                        }
                    }
                }
            }
        }
    }
}

auto Tile_renderer::get_multi_unit_tile(
    std::array<int, Battle_type::bit_count> battle_type_players
) const -> unit_tile_t
{
    int tile = s_multiple_unit_tile_offset;
    for (unsigned int battle_type = 0; battle_type < battle_type_players.size(); ++battle_type) {
        if (battle_type == Battle_type::bit_city) {
            continue;
        }
        int player_id = battle_type_players[battle_type];
        if (player_id == 0) {
            continue;
        }
        tile += s_multiple_unit_battle_type_multiplier[battle_type] * player_id;
    }
    return static_cast<unit_tile_t>(tile);
}

auto Tile_renderer::get_single_unit_tile(int player, unit_t unit) const -> unit_tile_t
{
    return static_cast<unit_tile_t>(
        s_single_unit_tile_offset +
        player * Unit_group::width * Unit_group::height +
        unit
    );
}

auto Tile_renderer::get_special_unit_tile(int special_unit_tile_index) const -> unit_tile_t
{
    return static_cast<unit_tile_t>(s_special_unit_tile_offset + special_unit_tile_index);
}

void Tile_renderer::compose_multiple_unit_tile
(
    Image&                                  scratch,
    const std::vector<Player_unit_colors>&  players_colors,
    std::array<int, Battle_type::bit_count> players,
    int                                     y0
)
{
    for (int y = 0; y < scratch.info.height; ++y) {
        for (int x = 0; x < scratch.info.width; ++x) {
            // Initialize tile pixel to transparant (black)
            scratch.put_pixel(x, y, glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});

            // Add contributions from each player
            int contribution_count = 0;
            for (int i = 0; i < players.size(); ++i) {
                if (i == Battle_type::bit_city) {
                    continue;
                }

                const int player = players[i];
                if (player == 0) {
                    continue;
                }
                const auto original_color = m_tileset_image.get_pixel(
                    i * Tile_shape::full_width + x,
                    y0 + y
                );
                if (original_color.a == 0) {
                    continue;
                }
                const glm::vec4 player_color =
                    original_color.b * players_colors[player - 1].shades[1] +
                    original_color.r * players_colors[player - 1].shades[2] +
                    original_color.g * players_colors[player - 1].shades[3];
                scratch.put_pixel(x, y, player_color);
                ++contribution_count;
                Expects(contribution_count < 2);
            }
        }
    }
}

auto Tile_renderer::get_terrain_shape(
    const terrain_tile_t terrain_tile
) const -> Pixel_coordinate
{
    Expects(terrain_tile < m_terrain_shapes.size());
    return m_terrain_shapes[terrain_tile];
}

auto Tile_renderer::get_unit_shape(
    const unit_tile_t unit_tile
) const -> Pixel_coordinate
{
    Expects(unit_tile < m_unit_shapes.size());
    return m_unit_shapes[unit_tile];
}

auto Tile_renderer::get_terrain_shapes() const -> const std::vector<Pixel_coordinate>&
{
    return m_terrain_shapes;
}

auto Tile_renderer::get_unit_shapes() const -> const std::vector<Pixel_coordinate>&
{
    return m_unit_shapes;
}

auto Tile_renderer::get_extra_shape(int extra) const -> Pixel_coordinate
{
    Expects(extra < m_extra_shapes.size());
    return m_extra_shapes.at(extra);
}

#if 0
auto Tile_renderer::get_terrain_tile(const terrain_t terrain_id) const -> tile_t
{
    return static_cast<tile_t>(m_terrain_tile_offset + terrain_id);
}

auto Tile_renderer::get_unit_tile(const unit_t unit_id) const -> tile_t
{
    return static_cast<tile_t>(m_unit_tile_offset + unit_id);
}

auto Tile_renderer::get_grid_tile(const int grid_mode) const -> tile_t
{
    return static_cast<tile_t>(grid_mode);
}
#endif

void Tile_renderer::create_frame_resources(size_t vertex_count)
{
    for (size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        m_frame_resources.emplace_back(
            m_graphics_instance,
            vertex_count,
            m_shader_stages,
            m_attribute_mappings,
            m_vertex_format,
            m_index_buffer,
            slot
        );
    }
}

auto Tile_renderer::current_frame_resources() -> Tile_renderer::Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Tile_renderer::next_frame()
{
    Expects(m_can_blit == false);

    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_vertex_writer    .reset();
    m_projection_writer.reset();
    m_index_range_first = 0;
    m_index_count       = 0;
}

void Tile_renderer::begin()
{
    Expects(m_can_blit == false);

    // TODO byte_count?
    const auto       vertex_gpu_data = m_vertex_writer.begin(&current_frame_resources().vertex_buffer, 0);
    std::byte* const start           = vertex_gpu_data.data()       + m_vertex_writer.write_offset;
    const size_t     byte_count      = vertex_gpu_data.size_bytes() - m_vertex_writer.write_offset;
    const size_t     word_count      = byte_count / sizeof(float);
    m_gpu_float_data = gsl::span<float>   {reinterpret_cast<float*   >(start), word_count};
    m_gpu_uint_data  = gsl::span<uint32_t>{reinterpret_cast<uint32_t*>(start), word_count};
    m_word_offset    = 0;

    m_can_blit = true;
}

auto Tile_renderer::tileset_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_tileset_texture;
}

auto Tile_renderer::tileset_image() const -> const Image&
{
    return m_tileset_image;
}

void Tile_renderer::blit(
    int      src_x,
    int      src_y,
    int      width,
    int      height,
    float    dst_x0,
    float    dst_y0,
    float    dst_x1,
    float    dst_y1,
    uint32_t color
)
{
    Expects(m_can_blit == true);

    const float u0 = static_cast<float>(src_x         ) / static_cast<float>(m_tileset_texture->width());
    const float v0 = static_cast<float>(src_y         ) / static_cast<float>(m_tileset_texture->height());
    const float u1 = static_cast<float>(src_x + width ) / static_cast<float>(m_tileset_texture->width());
    const float v1 = static_cast<float>(src_y + height) / static_cast<float>(m_tileset_texture->height());

    const float& x0 = dst_x0;
    const float& y0 = dst_y0;
    const float& x1 = dst_x1;
    const float& y1 = dst_y1;

    m_gpu_float_data[m_word_offset++] = x0;
    m_gpu_float_data[m_word_offset++] = y0;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u0;
    m_gpu_float_data[m_word_offset++] = v0;

    m_gpu_float_data[m_word_offset++] = x1;
    m_gpu_float_data[m_word_offset++] = y0;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u1;
    m_gpu_float_data[m_word_offset++] = v0;

    m_gpu_float_data[m_word_offset++] = x1;
    m_gpu_float_data[m_word_offset++] = y1;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u1;
    m_gpu_float_data[m_word_offset++] = v1;

    m_gpu_float_data[m_word_offset++] = x0;
    m_gpu_float_data[m_word_offset++] = y1;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u0;
    m_gpu_float_data[m_word_offset++] = v1;

    m_index_count += 5;
}

void Tile_renderer::end()
{
    Expects(m_can_blit == true);

    m_vertex_writer.write_offset += m_word_offset * 4;
    m_vertex_writer.end();

    m_can_blit = false;
}

static constexpr std::string_view c_tile_renderer_render{"Tile_renderer::render()"};

void Tile_renderer::render(erhe::math::Viewport viewport)
{
    if (m_index_count == 0) {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{c_tile_renderer_render};

    const auto handle = m_graphics_instance.get_handle(
        *m_tileset_texture.get(),
        m_nearest_sampler
    );
    //m_tileset_texture->get_handle();

    // TODO byte_count
    auto* const               projection_buffer   = &current_frame_resources().projection_buffer;
    const auto                projection_gpu_data = m_projection_writer.begin(projection_buffer, 0);
    std::byte* const          start               = projection_gpu_data.data()       + m_projection_writer.write_offset;
    const size_t              byte_count          = projection_gpu_data.size_bytes() - m_projection_writer.write_offset;
    const size_t              word_count          = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const glm::mat4 clip_from_window = erhe::math::create_orthographic(
        static_cast<float>(viewport.x), static_cast<float>(viewport.width),
        static_cast<float>(viewport.y), static_cast<float>(viewport.height),
        0.0f,
        1.0f
    );
    using erhe::graphics::as_span;
    using erhe::graphics::write;
    write(gpu_float_data, m_u_clip_from_window_offset, as_span(clip_from_window));

    m_projection_writer.write_offset += m_u_clip_from_window_size * sizeof(float);

    const uint32_t texture_handle[2] =
    {
        static_cast<uint32_t>((handle & 0xffffffffu)),
        static_cast<uint32_t>(handle >> 32u)
    };
    const gsl::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};
    write(gpu_uint32_data, m_u_texture_offset, texture_handle_cpu_data);
    m_projection_writer.write_offset += m_u_texture_size;

    m_projection_writer.end();

    const auto& pipeline = current_frame_resources().pipeline;

    //m_pipeline_state_tracker->shader_stages.reset();
    //m_pipeline_state_tracker->color_blend.execute(erhe::graphics::Color_blend_state::color_blend_disabled);
    //gl::invalidate_tex_image()
    gl::enable  (gl::Enable_cap::primitive_restart_fixed_index);
    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    m_graphics_instance.opengl_state_tracker.execute(pipeline);

    gl::bind_buffer_range(
        projection_buffer->target(),
        static_cast<GLuint>    (m_projection_block.binding_point()),
        static_cast<GLuint>    (projection_buffer->gl_name()),
        static_cast<GLintptr>  (m_projection_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_projection_writer.range.byte_count)
    );

    if (m_graphics_instance.info.use_bindless_texture) {
        gl::make_texture_handle_resident_arb(handle);
    } else {
        gl::bind_texture_unit(0, m_tileset_texture->gl_name());
        gl::bind_sampler     (0, m_nearest_sampler.gl_name());
    }

    gl::draw_elements(
        pipeline.data.input_assembly.primitive_topology,
        static_cast<GLsizei>(m_index_count),
        gl::Draw_elements_type::unsigned_int,
        reinterpret_cast<const void*>(m_index_range_first * 4)
    );

    if (m_graphics_instance.info.use_bindless_texture) {
        gl::make_texture_handle_non_resident_arb(handle);
    }

    gl::disable(gl::Enable_cap::primitive_restart_fixed_index);

    m_index_range_first += m_index_count;
    m_index_count = 0;
}

auto Tile_renderer::terrain_image(
    const terrain_tile_t terrain_tile,
    const int            scale
) -> bool
{
    const Pixel_coordinate& texel = get_terrain_shape(terrain_tile);
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(m_tileset_texture->width()),
        static_cast<float>(texel.y) / static_cast<float>(m_tileset_texture->height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(m_tileset_texture->width()),
        static_cast<float>(Tile_shape::height) / static_cast<float>(m_tileset_texture->height()),
    };

    return m_imgui_renderer.image(
        m_tileset_texture,
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

auto Tile_renderer::unit_image(
    const unit_tile_t unit_tile,
    const int         scale
) -> bool
{
    const auto&     texel = get_unit_shape(unit_tile);
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(m_tileset_texture->width()),
        static_cast<float>(texel.y) / static_cast<float>(m_tileset_texture->height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(m_tileset_texture->width()),
        static_cast<float>(Tile_shape::height    ) / static_cast<float>(m_tileset_texture->height()),
    };

    return m_imgui_renderer.image(
        m_tileset_texture,
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

void Tile_renderer::show_texture()
{
    const glm::vec2 uv0{0.0f, 0.0f};
    const glm::vec2 uv1{1.0f, 1.0f};

    m_imgui_renderer.image(
        m_tileset_texture,
        m_tileset_texture->width(),
        m_tileset_texture->height(),
        uv0,
        uv1,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

void Tile_renderer::make_terrain_type_combo(const char* label, terrain_t& value)
{
    auto&       preview_terrain = m_tiles.get_terrain_type(value);
    const char* preview_value   = preview_terrain.name.c_str();

    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge)) {
        const terrain_t end = static_cast<unit_t>(m_tiles.get_terrain_type_count());
        for (terrain_t i = 0; i < end; i++) {
            terrain_tile_t terrain_tile = m_tiles.get_terrain_tile_from_terrain(i);
            auto&          terrain_type = m_tiles.get_terrain_type(i);
            const auto     id           = fmt::format("##{}-{}", label, i);
            ImGui::PushID(id.c_str());
            bool is_selected = (value == i);
            if (terrain_image(terrain_tile, 1)) {
                value = i;
            }
            ImGui::SameLine();
            if (ImGui::Selectable(terrain_type.name.c_str(), is_selected)) {
                value = i;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

void Tile_renderer::make_unit_type_combo(
    const char* label,
    unit_t&     value,
    const int   player
)
{
    auto&       preview_unit  = m_tiles.get_unit_type(value);
    const char* preview_value = preview_unit.name.c_str();

    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge)) {
        const unit_t end = static_cast<unit_t>(m_tiles.get_unit_type_count());
        for (unit_t i = 0; i < end; i++) {
            unit_tile_t unit_tile = get_single_unit_tile(player, i);
            Unit_type&  unit_type = m_tiles.get_unit_type(i);
            const auto  id        = fmt::format("##{}-{}", label, i);
            ImGui::PushID(id.c_str());
            bool is_selected = (value == i);
            if (unit_image(unit_tile, 1)) {
                value = i;
            }
            ImGui::SameLine();
            if (ImGui::Selectable(unit_type.name.c_str(), is_selected)) {
                value = i;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

} // namespace hextiles



#if 0
void Tile_renderer::blit_tile(
    tile_t   tile,
    float    dst_x0,
    float    dst_y0,
    float    dst_x1,
    float    dst_y1,
    uint32_t color
)
{
    Expects(m_can_blit == true);

   //const uint32_t color = 0xffffffffu;
    const float& x0 = dst_x0;
    const float& y0 = dst_y0;
    const float& x1 = dst_x1;
    const float& y1 = dst_y1;

    m_gpu_float_data[m_word_offset++] = x0;
    m_gpu_float_data[m_word_offset++] = y0;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_uint_data [m_word_offset++] = tile;
    m_gpu_uint_data [m_word_offset++] = 0;

    m_gpu_float_data[m_word_offset++] = x1;
    m_gpu_float_data[m_word_offset++] = y0;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_uint_data [m_word_offset++] = tile;
    m_gpu_uint_data [m_word_offset++] = 1;

    m_gpu_float_data[m_word_offset++] = x1;
    m_gpu_float_data[m_word_offset++] = y1;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_uint_data [m_word_offset++] = tile;
    m_gpu_uint_data [m_word_offset++] = 2;

    m_gpu_float_data[m_word_offset++] = x0;
    m_gpu_float_data[m_word_offset++] = y1;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_uint_data [m_word_offset++] = tile;
    m_gpu_uint_data [m_word_offset++] = 3;
    m_index_count += 5;
}

    const size_t terrain_shape_count = m_tiles->get_terrain_shapes().size();
    const size_t unit_shape_count    = m_tiles->get_unit_shapes   ().size();
    const size_t unit_type_count     = m_tiles->get_unit_type_count();
    const size_t terrain_tile_count  = terrain_shape_count;
    const size_t unit_tile_count     = unit_shape_count * max_player_count;
    const size_t tile_count          = terrain_tile_count + unit_tile_count;

    erhe::graphics::Texture_create_info texture_create_info{
        .target          = gl::Texture_target::texture_2d_array,
        .internal_format = to_gl(m_tileset_image.info.format),
        .use_mipmaps     = false,
        .width           = Tile_shape::full_width,
        .height          = Tile_shape::height,
        .depth           = static_cast<int>(tile_count),
        .level_count     = 1
    };
    erhe::graphics::Texture_create_info texture_create_info{
        .target          = gl::Texture_target::texture_2d_array,
        .internal_format = to_gl(m_tileset_image.info.format),
        .use_mipmaps     = false,
        .width           = Tile_shape::full_width,
        .height          = Tile_shape::height,
        .depth           = static_cast<int>(tile_count),
        .level_count     = 1
    };

    m_terrain_tile_offset = 0;
    const auto& terrain_shapes = m_tiles->get_terrain_shapes();
    tile_t tile{0};
    for (terrain_t i = 0; i < terrain_tile_count; ++i) {
        auto&                   terrain_type = m_tiles->get_terrain_type(i);
        const Pixel_coordinate& shape        = terrain_shapes[i];
        terrain_type.tile = tile;
        m_tileset_texture->upload_subimage(
            to_gl(m_tileset_image.info.format),
            m_tileset_image.data,
            m_tileset_image.info.width,
            shape.x,
            shape.y,
            Tile_shape::full_width,
            Tile_shape::height,
            0,    // mipmap level
            0,    // x
            0,    // y
            tile  // z / array index
        );
        ++tile;
    }

    constexpr size_t player_color_shade_count = 4;
    struct Player_unit_colors
    {
        std::array<glm::vec4, player_color_shade_count> shades;
    };
    std::vector<Player_unit_colors> players_colors;
    {
        int ty_offset = 0;
        ty_offset += Tile_group::count * Tile_group::height; // terrain group tiles
        ty_offset += 1; // edge tiles
        ty_offset += 1; // extra tiles
        ty_offset += Base_tiles::height;
        ty_offset += 1; // grid tiles
        ty_offset += Explosion_tiles::height * 2; // Explision tiles (double width and height)
        int tx_offset = Unit_group::width;
        int x0 = tx_offset * Tile_shape::full_width;
        int y0 = ty_offset * Tile_shape::height;
        for (int i = 0; i < max_player_count; ++i) {
            Player_unit_colors player_colors;
            for (size_t s = 0; s < player_color_shade_count; ++s) {
                const glm::vec4 color = m_tileset_image.get_pixel(x0 + s, y0 + i);
                player_colors.shades[s] = color;
                log_c.tiles->info(
                    "Player {} Shader {} Color: {}, {}, {}, {}",
                    i, s,
                    color.r, color.g, color.b, color.a
                );
            }
            players_colors.push_back(player_colors);
        }
    }

    Image scratch{
        .info = {
            .width       = Unit_group::width * Tile_shape::full_width,
            .height      = Unit_group::height * Tile_shape::height,
            .depth       = 1,
            .level_count = 1,
            .row_stride  = 4 * Unit_group::width * Tile_shape::full_width,
            .format      = m_tileset_image.info.format
        }
    };
    scratch.data.resize(scratch.info.height * scratch.info.row_stride);

    //m_unit_tile_offset = tile;
    const auto& unit_shapes = m_tiles->get_unit_shapes();
    for (unit_t i = 0; i < unit_type_count; ++i) {
        auto&                   unit_type = m_tiles->get_unit_type(i);
        const Pixel_coordinate& shape     = unit_shapes[i];
        unit_type.tile = tile;
        for (int j = 0; j < max_player_count; ++j) {
            const Player_unit_colors& player_colors = players_colors[j];
            for (int y = 0; y < Tile_shape::height; ++y) {
                for (int x = 0; x < Tile_shape::full_width; ++x) {
                    const auto      original_color = m_tileset_image.get_pixel(x, y);
                    const glm::vec4 player_color   =
                        original_color.b * player_colors.shades[1] +
                        original_color.r * player_colors.shades[2] +
                        original_color.g * player_colors.shades[3];
                    scratch_tile.put_pixel(
                        x,
                        y,
                        original_color.b * player_colors.shades[1] +
                        original_color.r * player_colors.shades[2] +
                        original_color.g * player_colors.shades[3]
                    );
                }
            }
            m_tileset_texture->upload(
                to_gl(scratch_tile.info.format),
                scratch_tile.data,
                Tile_shape::full_width,
                Tile_shape::height,
                1,    // depth
                0,    // mipmap level
                0,    // x
                0,    // y
                tile  // z / array index
            );
        }
        tile++;
    }
#endif
