#include "map_renderer.hpp"
#include "texture_util.hpp"
//#include "log.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/scoped_buffer_mapping.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdarg>

namespace hextiles
{

using erhe::graphics::Shader_stages;

Map_renderer::Frame_resources::Frame_resources(
    const size_t                              vertex_count,
    erhe::graphics::Shader_stages*            shader_stages,
    erhe::graphics::Vertex_attribute_mappings attribute_mappings,
    erhe::graphics::Vertex_format&            vertex_format,
    erhe::graphics::Buffer*                   index_buffer,
    const size_t                              slot
)
    : vertex_buffer{
        gl::Buffer_target::array_buffer,
        vertex_format.stride() * vertex_count,
        storage_mask,
        access_mask
    }
    , projection_buffer{
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
            index_buffer
        )
    }
    , pipeline{
        {
            .name           = "Map renderer",
            .shader_stages  = shader_stages,
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

Map_renderer::Map_renderer()
    : Component{c_label}
{
}

Map_renderer::~Map_renderer()
{
}

void Map_renderer::connect()
{
    require<erhe::application::Gl_context_provider>();

    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

static constexpr std::string_view c_text_renderer_initialize_component{"Map_renderer::initialize_component()"};

void Map_renderer::initialize_component()
{
    const erhe::application::Scoped_gl_context gl_context{Component::get<erhe::application::Gl_context_provider>()};

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_initialize_component};

    m_projection_block = std::make_unique<erhe::graphics::Shader_resource>("projection", 0, erhe::graphics::Shader_resource::Type::uniform_block);

    constexpr gl::Buffer_storage_mask    storage_mask            {gl::Buffer_storage_mask::map_write_bit};
    constexpr gl::Map_buffer_access_mask access_mask             {gl::Map_buffer_access_mask::map_write_bit};
    constexpr size_t                     uint32_primitive_restart{0xffffffffu};
    constexpr size_t                     per_quad_vertex_count   {4}; // corner count
    constexpr size_t                     per_quad_index_count    {per_quad_vertex_count + 1}; // Plus one for primitive restart
    constexpr size_t                     max_quad_count          {1'000'000}; // each quad consumes 4 indices
    constexpr size_t                     index_count             {max_quad_count * per_quad_index_count};
    constexpr size_t                     index_stride            {4};

    m_index_buffer = std::make_unique<erhe::graphics::Buffer>(
        gl::Buffer_target::element_array_buffer,
        index_stride * index_count,
        storage_mask
    );

    erhe::graphics::Scoped_buffer_mapping<uint32_t> index_buffer_map{
        *m_index_buffer.get(),
        0,
        index_count,
        access_mask
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

    m_fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    m_attribute_mappings.add(
        {
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_position",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::position
            }
        }
    );
    m_attribute_mappings.add(
        {
            .layout_location = 1,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::color
            }
        }
    );

    m_attribute_mappings.add(
        {
            .layout_location = 2,
            .shader_type     = gl::Attribute_type::float_vec2,
            .name            = "a_texcoord",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::tex_coord
            }
        }
    );

    m_vertex_format.add(
        {
            .usage =
            {
                .type     = erhe::graphics::Vertex_attribute::Usage_type::position
            },
            .shader_type   = gl::Attribute_type::float_vec3,
            .data_type =
            {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 3
            }
        }
    );
    m_vertex_format.add(
        {
            .usage =
            {
                .type       = erhe::graphics::Vertex_attribute::Usage_type::color
            },
            .shader_type    = gl::Attribute_type::float_vec4,
            .data_type =
            {
                .type       = gl::Vertex_attrib_type::unsigned_byte,
                .normalized = true,
                .dimension  = 4
            }
        }
    );
    m_vertex_format.add(
        {
            .usage =
            {
                .type      = erhe::graphics::Vertex_attribute::Usage_type::tex_coord
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type =
            {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 2
            }
        }
    );

    m_nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::nearest,
        gl::Texture_mag_filter::nearest
    );

    const auto clip_from_window = m_projection_block->add_mat4 ("clip_from_window");
    const auto texture          = m_projection_block->add_uvec2("texture");
    m_u_clip_from_window_size   = clip_from_window->size_bytes();
    m_u_clip_from_window_offset = clip_from_window->offset_in_parent();
    m_u_texture_size            = texture->size_bytes();
    m_u_texture_offset          = texture->offset_in_parent();

    const auto shader_path = fs::path("res") / fs::path("shaders");
    const fs::path vs_path = shader_path / fs::path("tile.vert");
    const fs::path fs_path = shader_path / fs::path("tile.frag");
    Shader_stages::Create_info create_info{
        .name                      = "tile",
        .vertex_attribute_mappings = &m_attribute_mappings,
        .fragment_outputs          = &m_fragment_outputs,
    };
    create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    create_info.add_interface_block(m_projection_block.get());
    create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    Shader_stages::Prototype prototype{create_info};
    m_shader_stages = std::make_unique<Shader_stages>(std::move(prototype));

    create_frame_resources(max_quad_count * per_quad_vertex_count);

    const auto texture_path = fs::path("res") / fs::path("hextiles") / fs::path("hextiles.png");
    m_tileset_texture = load_texture(texture_path);
}

void Map_renderer::create_frame_resources(size_t vertex_count)
{
    for (size_t slot = 0; slot < s_frame_resources_count; ++slot)
    {
        m_frame_resources.emplace_back(
            vertex_count,
            m_shader_stages.get(),
            m_attribute_mappings,
            m_vertex_format,
            m_index_buffer.get(),
            slot
        );
    }
}

auto Map_renderer::current_frame_resources() -> Map_renderer::Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Map_renderer::next_frame()
{
    Expects(m_can_blit == false);

    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_vertex_writer    .reset();
    m_projection_writer.reset();
    m_index_range_first = 0;
    m_index_count       = 0;
}

void Map_renderer::begin()
{
    Expects(m_can_blit == false);

    m_vertex_writer.begin(current_frame_resources().vertex_buffer.target());

    const auto       vertex_gpu_data = current_frame_resources().vertex_buffer.map();
    std::byte* const start           = vertex_gpu_data.data()       + m_vertex_writer.write_offset;
    const size_t     byte_count      = vertex_gpu_data.size_bytes() - m_vertex_writer.write_offset;
    const size_t     word_count      = byte_count / sizeof(float);
    m_gpu_float_data = gsl::span<float>   {reinterpret_cast<float*   >(start), word_count};
    m_gpu_uint_data  = gsl::span<uint32_t>{reinterpret_cast<uint32_t*>(start), word_count};
    m_word_offset    = 0;

    m_can_blit = true;
}

auto Map_renderer::tileset_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_tileset_texture;
}

void Map_renderer::blit(
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

    const float    u0    = static_cast<float>(src_x         ) / static_cast<float>(m_tileset_texture->width());
    const float    v0    = static_cast<float>(src_y         ) / static_cast<float>(m_tileset_texture->height());
    const float    u1    = static_cast<float>(src_x + width ) / static_cast<float>(m_tileset_texture->width());
    const float    v1    = static_cast<float>(src_y + height) / static_cast<float>(m_tileset_texture->height());

    const float    z     = -0.5f;
    //const uint32_t color = 0xffffffffu;
    const float&   x0    = dst_x0;
    const float&   y0    = dst_y0;
    const float&   x1    = dst_x1;
    const float&   y1    = dst_y1;

    m_gpu_float_data[m_word_offset++] = x0;
    m_gpu_float_data[m_word_offset++] = y0;
    m_gpu_float_data[m_word_offset++] = z;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u0;
    m_gpu_float_data[m_word_offset++] = v0;

    m_gpu_float_data[m_word_offset++] = x1;
    m_gpu_float_data[m_word_offset++] = y0;
    m_gpu_float_data[m_word_offset++] = z;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u1;
    m_gpu_float_data[m_word_offset++] = v0;

    m_gpu_float_data[m_word_offset++] = x1;
    m_gpu_float_data[m_word_offset++] = y1;
    m_gpu_float_data[m_word_offset++] = z;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u1;
    m_gpu_float_data[m_word_offset++] = v1;

    m_gpu_float_data[m_word_offset++] = x0;
    m_gpu_float_data[m_word_offset++] = y1;
    m_gpu_float_data[m_word_offset++] = z;
    m_gpu_uint_data [m_word_offset++] = color;
    m_gpu_float_data[m_word_offset++] = u0;
    m_gpu_float_data[m_word_offset++] = v1;
    m_index_count += 5;
}

void Map_renderer::end()
{
    Expects(m_can_blit == true);

    m_vertex_writer.write_offset += m_word_offset * 4;
    m_vertex_writer.end();

    m_can_blit = false;
}

static constexpr std::string_view c_text_renderer_render{"Map_renderer::render()"};

void Map_renderer::render(erhe::scene::Viewport viewport)
{
    if (m_index_count == 0)
    {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_render};

    const auto handle = erhe::graphics::get_handle(*m_tileset_texture.get(), *m_nearest_sampler.get());
    //m_tileset_texture->get_handle();

    m_projection_writer.begin(current_frame_resources().projection_buffer.target());

    auto* const               projection_buffer   = &current_frame_resources().projection_buffer;
    const auto                projection_gpu_data = projection_buffer->map();
    std::byte* const          start               = projection_gpu_data.data()       + m_projection_writer.write_offset;
    const size_t              byte_count          = projection_gpu_data.size_bytes() - m_projection_writer.write_offset;
    const size_t              word_count          = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const glm::mat4 clip_from_window = erhe::toolkit::create_orthographic(
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
    m_pipeline_state_tracker->execute(pipeline);

    gl::bind_buffer_range(
        projection_buffer->target(),
        static_cast<GLuint>    (m_projection_block->binding_point()),
        static_cast<GLuint>    (projection_buffer->gl_name()),
        static_cast<GLintptr>  (m_projection_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_projection_writer.range.byte_count)
    );

    gl::make_texture_handle_resident_arb(handle);
    gl::draw_elements(
        pipeline.data.input_assembly.primitive_topology,
        static_cast<GLsizei>(m_index_count),
        gl::Draw_elements_type::unsigned_int,
        reinterpret_cast<const void*>(m_index_range_first * 4)
    );
    gl::make_texture_handle_non_resident_arb(handle);
    gl::disable(gl::Enable_cap::primitive_restart_fixed_index);

    m_index_range_first += m_index_count;
    m_index_count = 0;
}

} // namespace hextiles
