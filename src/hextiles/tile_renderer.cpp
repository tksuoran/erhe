#include "tile_renderer.hpp"

#include "tiles.hpp"
#include "hextiles_log.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/scoped_buffer_mapping.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

#include <cstdarg>

namespace hextiles {

using erhe::graphics::Shader_stages;

constexpr size_t uint32_primitive_restart{0xffffffffu};
constexpr size_t per_quad_vertex_count   {4}; // corner count
constexpr size_t per_quad_index_count    {per_quad_vertex_count + 1}; // Plus one for primitive restart
constexpr size_t max_quad_count          {1'000'000}; // each quad consumes 4 indices
constexpr size_t index_count             {max_quad_count * per_quad_index_count};
constexpr size_t index_stride            {4};

auto Tile_renderer::make_prototype(erhe::graphics::Device& graphics_device) const -> erhe::graphics::Shader_stages_prototype
{
    erhe::graphics::Shader_stages_create_info create_info{
        .name                  = "tile",
        .interface_blocks      = { &m_projection_block },
        .fragment_outputs      = &m_fragment_outputs,
        .vertex_format         = &m_vertex_format,
        .default_uniform_block = m_graphics_device.get_info().use_bindless_texture
            ? nullptr
            : &m_default_uniform_block,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   m_shader_path / std::filesystem::path{"tile.vert"} },
            { erhe::graphics::Shader_type::fragment_shader, m_shader_path / std::filesystem::path{"tile.frag"} }
        },
        .dump_interface    = true,
        .dump_final_source = true
    };

    if (m_graphics_device.get_info().use_bindless_texture) {
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        create_info.extensions.push_back({erhe::graphics::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    }

    return erhe::graphics::Shader_stages_prototype{graphics_device, create_info};
}

auto Tile_renderer::make_program(erhe::graphics::Shader_stages_prototype&& prototype) const -> erhe::graphics::Shader_stages
{
    if (!prototype.is_valid()) {
        log_startup->error("current directory is {}", std::filesystem::current_path().string());
        log_startup->error("Compiling shader program {} failed", prototype.name());
        return erhe::graphics::Shader_stages{m_graphics_device, prototype.name()};
    }

    return erhe::graphics::Shader_stages{m_graphics_device, std::move(prototype)};
}

Tile_renderer::Tile_renderer(
    erhe::graphics::Device&      graphics_device,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    Tiles&                       tiles
)
    : m_graphics_device      {graphics_device}
    , m_imgui_renderer       {imgui_renderer}
    , m_tiles                {tiles}
    , m_default_uniform_block{graphics_device}
    , m_texture_sampler{
        graphics_device.get_info().use_bindless_texture
            ? nullptr
            : m_default_uniform_block.add_sampler("s_texture", erhe::graphics::Glsl_type::sampler_2d, 0)
    }
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , m_vertex_format{
        {
            0,
            {
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::position    },
                { erhe::dataformat::Format::format_8_vec4_unorm,  erhe::dataformat::Vertex_attribute_usage::color       },
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord, 0}
            }
        }
    }
    , m_index_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count = index_stride * index_count,
            .usage               = erhe::graphics::Buffer_usage::index,
            .direction           = erhe::graphics::Buffer_direction::cpu_to_gpu,
            .cache_mode          = erhe::graphics::Buffer_cache_mode::default_,
            .mapping             = erhe::graphics::Buffer_mapping::transient,
            .coherency           = erhe::graphics::Buffer_coherency::off,
            .debug_label         = "Tile_renderer index buffer"
        }
    }
    , m_nearest_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Tile_renderer"
        }
    }
    , m_projection_block{
        graphics_device,
        "projection",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_clip_from_window         {m_projection_block.add_mat4 ("clip_from_window")}
    , m_texture_handle           {m_projection_block.add_uvec2("texture")}
    , m_u_clip_from_window_size  {m_clip_from_window->get_size_bytes()}
    , m_u_clip_from_window_offset{m_clip_from_window->get_offset_in_parent()}
    , m_u_texture_size           {m_texture_handle  ->get_size_bytes()}
    , m_u_texture_offset         {m_texture_handle  ->get_offset_in_parent()}
    , m_shader_path              {std::filesystem::path{"res"} / std::filesystem::path{"shaders"}}
    , m_shader_stages            {make_program(make_prototype(graphics_device))}
    , m_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::vertex,
        "Tile_renderer::m_vertex_buffer",
    }
    , m_projection_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Tile_renderer::m_projection_buffer",
        m_projection_block.get_binding_point()
    }
    , m_vertex_input{m_graphics_device, erhe::graphics::Vertex_input_state_data::make(m_vertex_format)}
    , m_pipeline{
        erhe::graphics::Render_pipeline_data{
            .name           = "Map renderer",
            .shader_stages  = &m_shader_stages,
            .vertex_input   = &m_vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle_strip,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied,
        }
    }
{
    // Prefill index buffer
    erhe::graphics::Scoped_buffer_mapping<uint32_t> index_buffer_map{
        m_index_buffer, 0, index_count, erhe::graphics::Buffer_map_flags::none
    };

    const auto& gpu_index_data = index_buffer_map.span();
    size_t      offset      {0};
    uint32_t    vertex_index{0};
    for (unsigned int i = 0; i < max_quad_count; ++i) {
        // This used to be triangle fan but is now triangle strip
        gpu_index_data[offset + 0] = vertex_index;
        gpu_index_data[offset + 1] = vertex_index + 1;
        gpu_index_data[offset + 2] = vertex_index + 3;
        gpu_index_data[offset + 3] = vertex_index + 2;
        gpu_index_data[offset + 4] = uint32_primitive_restart;
        vertex_index += 4;
        offset += 5;
    }

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
        .device      = m_graphics_device,
        .type        = erhe::graphics::Texture_type::texture_2d,
        .pixelformat = m_tileset_image.info.format,
        .use_mipmaps = false,
        .width       = m_tileset_image.info.width,
        .height      = ty_offset * Tile_shape::height,
        .depth       = 1,
        .level_count = 1,
        .debug_label = fmt::format("Tile_renderer::m_tileset_texture {}", texture_path.string())
    };

    m_tileset_texture = std::make_shared<erhe::graphics::Texture>(m_graphics_device, texture_create_info);
    m_graphics_device.clear_texture(*m_tileset_texture.get(), { 1.0, 0.0, 1.0, 1.0 });

    // Upload everything before single unit tiles
    erhe::graphics::Blit_command_encoder encoder{m_graphics_device};
    erhe::graphics::GPU_ring_buffer_client texture_upload_buffer{
        m_graphics_device,
        erhe::graphics::Buffer_target::pixel,
        "Tile_renderer::m_tileset_texture upload"
    };
    {
        std::span<std::uint8_t>      src_span{m_tileset_image.data.data(), m_tileset_image.data.size()};
        std::size_t                  byte_count   = src_span.size_bytes();
        erhe::graphics::Buffer_range buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
        std::span<std::byte>         dst_span     = buffer_range.get_span();
        memcpy(dst_span.data(), src_span.data(), byte_count);
        buffer_range.bytes_written(byte_count);
        buffer_range.close();

        const std::size_t src_bytes_per_row   = m_tileset_image.info.width  * erhe::dataformat::get_format_size(m_tileset_image.info.format);
        const std::size_t src_bytes_per_image = m_tileset_image.info.height * src_bytes_per_row;
        encoder.copy_from_buffer(
            buffer_range.get_buffer()->get_buffer(),          // source_buffer
            buffer_range.get_byte_start_offset_in_buffer(),   // source_offset
            src_bytes_per_row,                                // source_bytes_per_row
            src_bytes_per_image,                              // source_bytes_per_image
            glm::ivec3{m_tileset_image.info.width, ty0_single_unit_tiles * Tile_shape::height, 1}, // source_size
            m_tileset_texture.get(),                          // destination_texture
            0,                                                // destination_slice
            0,                                                // destination_level
            glm::ivec3{0, 0, 0}                               // destination_origin
        );
        buffer_range.release();
    }

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
        const int src_bytes_per_row   = scratch.info.row_stride;
        const int src_bytes_per_image = scratch.info.height * src_bytes_per_row;
        std::span<std::uint8_t> src_span{scratch.data.data(), scratch.data.size()};

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

            erhe::graphics::Buffer_range buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, src_bytes_per_image);
            std::span<std::byte>         dst_span     = buffer_range.get_span();
            memcpy(dst_span.data(), src_span.data(), src_bytes_per_image);
            buffer_range.bytes_written(src_bytes_per_image);
            buffer_range.close();
            encoder.copy_from_buffer(
                buffer_range.get_buffer()->get_buffer(),          // source_buffer
                buffer_range.get_byte_start_offset_in_buffer(),   // source_offset
                src_bytes_per_row,                                // source_bytes_per_row
                src_bytes_per_image,                              // source_bytes_per_image
                glm::ivec3{m_tileset_image.info.width, ty0_single_unit_tiles * Tile_shape::height, 1}, // source_size
                m_tileset_texture.get(),                          // destination_texture
                0,                                                // destination_slice
                0,                                                // destination_level
                glm::ivec3{0, 0, 0}                               // destination_origin
            );
            buffer_range.release();
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
        const int src_bytes_per_row   = scratch.info.row_stride;
        const int src_bytes_per_image = scratch.info.height * src_bytes_per_row;
        std::span<std::uint8_t> src_span{scratch.data.data(), scratch.data.size()};

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

                        erhe::graphics::Buffer_range buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, src_bytes_per_image);
                        std::span<std::byte>         dst_span     = buffer_range.get_span();
                        memcpy(dst_span.data(), src_span.data(), src_bytes_per_image);
                        buffer_range.bytes_written(src_bytes_per_image);
                        buffer_range.close();
                        encoder.copy_from_buffer(
                            buffer_range.get_buffer()->get_buffer(),          // source_buffer
                            buffer_range.get_byte_start_offset_in_buffer(),   // source_offset
                            src_bytes_per_row,                                // source_bytes_per_row
                            src_bytes_per_image,                              // source_bytes_per_image
                            glm::ivec3{m_tileset_image.info.width, ty0_single_unit_tiles * Tile_shape::height, 1}, // source_size
                            m_tileset_texture.get(),                          // destination_texture
                            0,                                                // destination_slice
                            0,                                                // destination_level
                            glm::ivec3{0, 0, 0}                               // destination_origin
                        );
                        buffer_range.release();

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

auto Tile_renderer::get_multi_unit_tile(std::array<int, Battle_type::bit_count> battle_type_players) const -> unit_tile_t
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

void Tile_renderer::compose_multiple_unit_tile(
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
                ERHE_VERIFY(contribution_count < 2);
            }
        }
    }
}

auto Tile_renderer::get_terrain_shape(const terrain_tile_t terrain_tile) const -> Pixel_coordinate
{
    ERHE_VERIFY(terrain_tile < m_terrain_shapes.size());
    return m_terrain_shapes[terrain_tile];
}

auto Tile_renderer::get_unit_shape(const unit_tile_t unit_tile) const -> Pixel_coordinate
{
    ERHE_VERIFY(unit_tile < m_unit_shapes.size());
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
    ERHE_VERIFY(extra < m_extra_shapes.size());
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

void Tile_renderer::begin(std::size_t tile_count)
{
    ERHE_VERIFY(m_can_blit == false);
    ERHE_VERIFY(!m_vertex_buffer_range.has_value());

    const std::size_t byte_count = tile_count * 5 * 4 * sizeof(uint32_t); // 20 words per tile
    m_vertex_buffer_range = m_vertex_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
    m_index_count = 0;

    // TODO byte_count?
    const std::span<std::byte> vertex_gpu_data = m_vertex_buffer_range.value().get_span();
    std::byte* const           start           = vertex_gpu_data.data();
    ERHE_VERIFY(byte_count <= vertex_gpu_data.size_bytes());
    const size_t               word_count      = byte_count / sizeof(float);
    m_gpu_float_data = std::span<float>   {reinterpret_cast<float*   >(start), word_count};
    m_gpu_uint_data  = std::span<uint32_t>{reinterpret_cast<uint32_t*>(start), word_count};
    m_word_offset    = 0;

    m_can_blit = true;
    m_tile_blit_count = 0;
    m_reserved_tile_count = tile_count;
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
    ERHE_VERIFY(m_can_blit == true);

    ERHE_VERIFY(m_tile_blit_count < m_reserved_tile_count);

    const float u0 = static_cast<float>(src_x         ) / static_cast<float>(m_tileset_texture->get_width());
    const float v0 = static_cast<float>(src_y         ) / static_cast<float>(m_tileset_texture->get_height());
    const float u1 = static_cast<float>(src_x + width ) / static_cast<float>(m_tileset_texture->get_width());
    const float v1 = static_cast<float>(src_y + height) / static_cast<float>(m_tileset_texture->get_height());

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
    ++m_tile_blit_count;
}

void Tile_renderer::end()
{
    ERHE_VERIFY(m_can_blit == true);

    ERHE_VERIFY(m_vertex_buffer_range.has_value());
    size_t byte_count = m_word_offset * sizeof(float);
    m_vertex_buffer_range.value().bytes_written(byte_count);
    m_vertex_buffer_range.value().close();
    m_can_blit = false;
}

void Tile_renderer::render(erhe::graphics::Render_command_encoder& render_encoder, erhe::math::Viewport viewport)
{
    if (m_index_count == 0) {
        return;
    }
    if (!m_vertex_buffer_range.has_value()) {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{"Tile_renderer::render()"};

    const auto handle = m_graphics_device.get_handle(*m_tileset_texture.get(), m_nearest_sampler);

    erhe::graphics::Buffer_range& vertex_buffer_range     = m_vertex_buffer_range.value();
    erhe::graphics::Buffer_range  projection_buffer_range = m_projection_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, m_projection_block.get_size_bytes());
    const auto                    projection_gpu_data     = projection_buffer_range.get_span();
    size_t                        projection_write_offset = 0;
    std::byte* const              start                   = projection_gpu_data.data();
    const size_t                  byte_count              = projection_gpu_data.size_bytes();
    const size_t                  word_count              = byte_count / sizeof(float);
    const std::span<float>        gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t>     gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const glm::mat4 clip_from_window = erhe::math::create_orthographic(
        static_cast<float>(viewport.x), static_cast<float>(viewport.width),
        static_cast<float>(viewport.y), static_cast<float>(viewport.height),
        0.0f,
        1.0f
    );
    using erhe::graphics::as_span;
    using erhe::graphics::write;
    write(gpu_float_data, m_u_clip_from_window_offset, as_span(clip_from_window));

    projection_write_offset += m_u_clip_from_window_size;

    const uint32_t texture_handle[2] =
    {
        static_cast<uint32_t>((handle & 0xffffffffu)),
        static_cast<uint32_t>(handle >> 32u)
    };
    const std::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};
    write(gpu_uint32_data, m_u_texture_offset, texture_handle_cpu_data);
    projection_write_offset += m_u_texture_size;

    projection_buffer_range.bytes_written(projection_write_offset);
    projection_buffer_range.close();

    //m_pipeline_state_tracker->shader_stages.reset();
    //m_pipeline_state_tracker->color_blend.execute(erhe::graphics::Color_blend_state::color_blend_disabled);
    //gl::invalidate_tex_image()
    erhe::graphics::Buffer* vertex_buffer = vertex_buffer_range.get_buffer()->get_buffer();

    render_encoder.set_render_pipeline_state(m_pipeline);
    render_encoder.set_index_buffer(&m_index_buffer);
    render_encoder.set_vertex_buffer(vertex_buffer, vertex_buffer_range.get_byte_start_offset_in_buffer(), 0);

    m_projection_buffer.bind(render_encoder, projection_buffer_range);

    erhe::graphics::Texture_heap texture_heap{m_graphics_device, *m_tileset_texture.get(), m_nearest_sampler, 1};
    texture_heap.assign(0, m_tileset_texture.get(), &m_nearest_sampler);
    texture_heap.bind();

    render_encoder.draw_indexed_primitives(
        m_pipeline.data.input_assembly.primitive_topology,
        m_index_count,
        erhe::dataformat::Format::format_32_scalar_uint,
        0
    );

    projection_buffer_range.release();
    vertex_buffer_range.release();

    texture_heap.unbind();

    m_vertex_buffer_range.reset();
}

auto Tile_renderer::terrain_image(const terrain_tile_t terrain_tile, const int scale) -> bool
{
    const Pixel_coordinate& texel = get_terrain_shape(terrain_tile);
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(m_tileset_texture->get_width()),
        static_cast<float>(texel.y) / static_cast<float>(m_tileset_texture->get_height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(m_tileset_texture->get_width()),
        static_cast<float>(Tile_shape::height    ) / static_cast<float>(m_tileset_texture->get_height()),
    };

    return m_imgui_renderer.image(
        m_tileset_texture.get(),
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    );
}

auto Tile_renderer::unit_image(const unit_tile_t unit_tile, const int scale) -> bool
{
    const auto&     texel = get_unit_shape(unit_tile);
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(m_tileset_texture->get_width()),
        static_cast<float>(texel.y) / static_cast<float>(m_tileset_texture->get_height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(m_tileset_texture->get_width()),
        static_cast<float>(Tile_shape::height    ) / static_cast<float>(m_tileset_texture->get_height()),
    };

    return m_imgui_renderer.image(
        m_tileset_texture.get(),
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    );
}

void Tile_renderer::show_texture()
{
    const glm::vec2 uv0{0.0f, 0.0f};
    const glm::vec2 uv1{1.0f, 1.0f};

    m_imgui_renderer.image(
        m_tileset_texture.get(),
        m_tileset_texture->get_width(),
        m_tileset_texture->get_height(),
        uv0,
        uv1,
        glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode::not_mipmapped
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

void Tile_renderer::make_unit_type_combo(const char* label, unit_t& value, const int player)
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
