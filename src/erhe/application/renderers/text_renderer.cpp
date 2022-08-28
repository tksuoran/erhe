#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/scoped_buffer_mapping.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/ui/font.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iomanip>
#include <cstdarg>

namespace erhe::application
{

using erhe::graphics::Shader_stages;
using glm::mat4;
using glm::vec3;
using glm::vec4;

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

Text_renderer::Frame_resources::Frame_resources(
    const std::size_t                         vertex_count,
    erhe::graphics::Shader_stages*            shader_stages,
    erhe::graphics::Vertex_attribute_mappings attribute_mappings,
    erhe::graphics::Vertex_format&            vertex_format,
    erhe::graphics::Buffer*                   index_buffer,
    const std::size_t                         slot
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
            .name           = "Text renderer",
            .shader_stages  = shader_stages,
            .vertex_input   = &vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied,
        }
    }
{
    vertex_buffer    .set_debug_label(fmt::format("Text Renderer Vertex {}", slot));
    projection_buffer.set_debug_label(fmt::format("Text Renderer Projection {}", slot));
}

Text_renderer::Text_renderer()
    : Component{c_type_name}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , m_attribute_mappings{
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_position",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::position
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 1,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::color
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 2,
            .shader_type     = gl::Attribute_type::float_vec2,
            .name            = "a_texcoord",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::tex_coord
            }
        }
    }
    , m_vertex_format{
        erhe::graphics::Vertex_attribute{
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
        },
        erhe::graphics::Vertex_attribute{
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
        },
        erhe::graphics::Vertex_attribute{
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
    }
{
}

Text_renderer::~Text_renderer() noexcept
{
}

void Text_renderer::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<Gl_context_provider>();
}

static constexpr std::string_view c_text_renderer_initialize_component{"Text_renderer::initialize_component()"};

void Text_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& config = get<erhe::application::Configuration>();

    if (!config->text_renderer.enabled)
    {
        log_startup->info("Text renderer disabled due to erhe.ini setting");
        return;
    }

    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_initialize_component};

    m_projection_block = std::make_unique<erhe::graphics::Shader_resource>("projection", 0, erhe::graphics::Shader_resource::Type::uniform_block);

    constexpr std::size_t uint16_max              {65535};
    constexpr std::size_t uint16_primitive_restart{0xffffu};
    constexpr std::size_t per_quad_vertex_count   {4}; // corner count
    constexpr std::size_t per_quad_index_count    {per_quad_vertex_count + 1}; // Plus one for primitive restart
    constexpr std::size_t max_quad_count          {uint16_max / per_quad_vertex_count}; // each quad consumes 4 indices
    constexpr std::size_t index_count             {uint16_max * per_quad_index_count};
    constexpr std::size_t index_stride            {2};

    m_index_buffer = std::make_unique<erhe::graphics::Buffer>(
        gl::Buffer_target::element_array_buffer,
        index_stride * index_count,
        gl::Buffer_storage_mask::map_write_bit
    );

    erhe::graphics::Scoped_buffer_mapping<uint16_t> index_buffer_map{
        *m_index_buffer.get(),
        0,
        index_count,
        gl::Map_buffer_access_mask::map_write_bit
    };

    const auto& gpu_index_data = index_buffer_map.span();
    std::size_t offset      {0};
    uint16_t    vertex_index{0};
    for (unsigned int i = 0; i < max_quad_count; ++i)
    {
        gpu_index_data[offset + 0] = vertex_index;
        gpu_index_data[offset + 1] = vertex_index + 1;
        gpu_index_data[offset + 2] = vertex_index + 2;
        gpu_index_data[offset + 3] = vertex_index + 3;
        gpu_index_data[offset + 4] = uint16_primitive_restart;
        vertex_index += 4;
        offset += 5;
    }

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

    m_font = std::make_unique<erhe::ui::Font>("res/fonts/SourceSansPro-Regular.otf", 12, 0.4f);

    {
        ERHE_PROFILE_SCOPE("shader");

        const auto shader_path = fs::path("res") / fs::path("shaders");
        const fs::path vs_path = shader_path / fs::path("text.vert");
        const fs::path fs_path = shader_path / fs::path("text.frag");
        Shader_stages::Create_info create_info{
            .name                      = "text",
            .interface_blocks          = { m_projection_block.get() },
            .vertex_attribute_mappings = &m_attribute_mappings,
            .fragment_outputs          = &m_fragment_outputs,
            .shaders = {
                { gl::Shader_type::vertex_shader,   vs_path },
                { gl::Shader_type::fragment_shader, fs_path }
            }
        };

        if (erhe::graphics::Instance::info.use_bindless_texture)
        {
            create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
            create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        }
        else
        {
            m_default_uniform_block.add_sampler(
                "s_texture",
                gl::Uniform_type::sampler_2d,
                0
            );
            create_info.default_uniform_block = &m_default_uniform_block;
        }

        Shader_stages::Prototype prototype{create_info};
        if (
            !prototype.is_valid()
            //prototype.m_handle.gl_name() == 0 ||
            //prototype.m_attached_shaders.empty()
        )
        {
            log_startup->error("Text renderer shader compilation failed");
            return;
        }

        m_shader_stages = std::make_unique<Shader_stages>(std::move(prototype));
    }

    create_frame_resources();
}

void Text_renderer::post_initialize()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Text_renderer::create_frame_resources()
{
    ERHE_PROFILE_FUNCTION

    constexpr std::size_t vertex_count{65536};
    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot)
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


auto Text_renderer::current_frame_resources() -> Text_renderer::Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Text_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_vertex_writer    .reset();
    m_projection_writer.reset();
    m_index_range_first = 0;
    m_index_count       = 0;
}

void Text_renderer::print(
    const glm::vec3        text_position,
    const uint32_t         text_color,
    const std::string_view text)
{
    ERHE_PROFILE_FUNCTION

    if (!m_font)
    {
        return;
    }

    m_vertex_writer.begin(current_frame_resources().vertex_buffer.target());

    const auto                vertex_gpu_data = current_frame_resources().vertex_buffer.map();
    std::byte* const          start           = vertex_gpu_data.data()       + m_vertex_writer.write_offset;
    const std::size_t         byte_count      = vertex_gpu_data.size_bytes() - m_vertex_writer.write_offset;
    const std::size_t         word_count      = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    erhe::ui::Rectangle bounding_box;
    const vec3          snapped_position{
        std::floor(text_position.x + 0.5f),
        std::floor(text_position.y + 0.5f),
        text_position.z
    };
    const std::size_t quad_count = m_font->print(
        gpu_float_data,
        gpu_uint_data,
        text,
        snapped_position,
        text_color,
        bounding_box
    );
    m_vertex_writer.write_offset += quad_count * 4 * m_vertex_format.stride();
    m_vertex_writer.end();
    m_index_count += quad_count * 5;
}

auto Text_renderer::measure(const std::string_view text) const -> erhe::ui::Rectangle
{
    return m_font
        ? m_font->measure(text)
        : erhe::ui::Rectangle{};
}

static constexpr std::string_view c_text_renderer_render{"Text_renderer::render()"};
void Text_renderer::render(erhe::scene::Viewport viewport)
{
    if (m_index_count == 0)
    {
        return;
    }

    ERHE_PROFILE_FUNCTION

    ERHE_PROFILE_GPU_SCOPE(c_text_renderer_render)

    const auto handle = erhe::graphics::get_handle(
        *m_font->texture().get(),
        *m_nearest_sampler.get()
    );

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_render};

    m_projection_writer.begin(current_frame_resources().projection_buffer.target());

    auto* const               projection_buffer   = &current_frame_resources().projection_buffer;
    const auto                projection_gpu_data = projection_buffer->map();
    std::byte* const          start               = projection_gpu_data.data()       + m_projection_writer.write_offset;
    const std::size_t         byte_count          = projection_gpu_data.size_bytes() - m_projection_writer.write_offset;
    const std::size_t         word_count          = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const mat4 clip_from_window = erhe::toolkit::create_orthographic(
        static_cast<float>(viewport.x), static_cast<float>(viewport.width),
        static_cast<float>(viewport.y), static_cast<float>(viewport.height),
        0.0f,
        1.0f
    );
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    write(gpu_float_data, m_u_clip_from_window_offset, as_span(clip_from_window));
    m_projection_writer.write_offset += m_u_clip_from_window_size;

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

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(erhe::graphics::Color_blend_state::color_blend_disabled);
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

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        gl::make_texture_handle_resident_arb(handle);
    }
    else
    {
        gl::bind_texture_unit(0, m_font->texture()->gl_name());
        gl::bind_sampler(0, m_nearest_sampler->gl_name());
    }

    gl::draw_elements(
        pipeline.data.input_assembly.primitive_topology,
        static_cast<GLsizei>(m_index_count),
        gl::Draw_elements_type::unsigned_short,
        reinterpret_cast<const void*>(m_index_range_first * 2)
    );

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        gl::make_texture_handle_non_resident_arb(handle);
    }

    gl::disable(gl::Enable_cap::primitive_restart_fixed_index);

    m_index_range_first += m_index_count;
    m_index_count = 0;
}

} // namespace erhe::application
