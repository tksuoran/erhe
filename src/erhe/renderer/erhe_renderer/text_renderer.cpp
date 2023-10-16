#include "erhe_renderer/text_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/scoped_buffer_mapping.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_ui/font.hpp"

namespace erhe::renderer
{

using glm::mat4;
using glm::vec3;
using glm::vec4;

namespace {

static constexpr gl::Buffer_storage_mask storage_mask_persistent{
    gl::Buffer_storage_mask::map_coherent_bit   |
    gl::Buffer_storage_mask::map_persistent_bit |
    gl::Buffer_storage_mask::map_write_bit
};
static constexpr gl::Buffer_storage_mask storage_mask_not_persistent{
    gl::Buffer_storage_mask::map_write_bit
};
inline auto storage_mask(erhe::graphics::Instance& graphics_instance) -> gl::Buffer_storage_mask
{
    return graphics_instance.info.use_persistent_buffers
        ? storage_mask_persistent
        : storage_mask_not_persistent;
}

static constexpr gl::Map_buffer_access_mask access_mask_persistent{
    gl::Map_buffer_access_mask::map_coherent_bit   |
    gl::Map_buffer_access_mask::map_persistent_bit |
    gl::Map_buffer_access_mask::map_write_bit
};
static constexpr gl::Map_buffer_access_mask access_mask_not_persistent{
    gl::Map_buffer_access_mask::map_write_bit
};
inline auto access_mask(erhe::graphics::Instance& graphics_instance) -> gl::Map_buffer_access_mask
{
    return graphics_instance.info.use_persistent_buffers
        ? access_mask_persistent
        : access_mask_not_persistent;
}

}

Text_renderer::Frame_resources::Frame_resources(
    erhe::graphics::Instance&                  graphics_instance,
    const bool                                 reverse_depth,
    const std::size_t                          vertex_count,
    erhe::graphics::Shader_stages*             shader_stages,
    erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
    erhe::graphics::Vertex_format&             vertex_format,
    erhe::graphics::Buffer&                    index_buffer,
    const std::size_t                          slot
)
    : vertex_buffer{
        graphics_instance,
        gl::Buffer_target::array_buffer,
        vertex_format.stride() * vertex_count,
        storage_mask(graphics_instance),
        access_mask(graphics_instance)
    }
    , projection_buffer{
        graphics_instance,
        gl::Buffer_target::uniform_buffer,
        1024, // TODO
        storage_mask(graphics_instance),
        access_mask(graphics_instance)
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
        {
            .name           = "Text renderer",
            .shader_stages  = shader_stages,
            .vertex_input   = &vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied,
        }
    }
{
    vertex_buffer    .set_debug_label(fmt::format("Text Renderer Vertex {}", slot));
    projection_buffer.set_debug_label(fmt::format("Text Renderer Projection {}", slot));
}

static constexpr std::string_view c_text_renderer_initialize_component{"Text_renderer::initialize_component()"};

Text_renderer::Text_renderer(
    erhe::graphics::Instance& graphics_instance
)
    : m_graphics_instance{graphics_instance}
    , m_default_uniform_block{graphics_instance}
   ,  m_projection_block{
        graphics_instance,
        "projection",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_clip_from_window_resource{m_projection_block.add_mat4 ("clip_from_window")}
    , m_texture_resource         {m_projection_block.add_uvec2("texture")}
    , m_u_clip_from_window_size  {m_clip_from_window_resource->size_bytes()}
    , m_u_clip_from_window_offset{m_clip_from_window_resource->offset_in_parent()}
    , m_u_texture_size           {m_texture_resource->size_bytes()}
    , m_u_texture_offset         {m_texture_resource->offset_in_parent()}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , m_attribute_mappings{
        graphics_instance,
        { // initializer list
            erhe::graphics::Vertex_attribute_mapping::a_position_float_vec3(),
            erhe::graphics::Vertex_attribute_mapping::a_color_float_vec4(),
            erhe::graphics::Vertex_attribute_mapping::a_texcoord_float_vec2()
        }
    }
    , m_vertex_format{
        erhe::graphics::Vertex_attribute::position_float3(),
        erhe::graphics::Vertex_attribute::color_ubyte4(),
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
            .debug_label = "Text_renderer"
        }
    }
    , m_vertex_writer        {graphics_instance}
    , m_projection_writer    {graphics_instance}
{
    ERHE_PROFILE_FUNCTION();

    auto ini = erhe::configuration::get_ini("erhe.ini", "text_renderer");
    ini->get("enabled",   config.enabled);
    ini->get("font_size", config.font_size);

    if (!config.enabled) {
        log_startup->info("Text renderer disabled due to erhe.ini setting");
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_initialize_component};

    erhe::graphics::Scoped_buffer_mapping<uint16_t> index_buffer_map{
        m_index_buffer,
        0,
        index_count,
        gl::Map_buffer_access_mask::map_write_bit
    };

    const auto& gpu_index_data = index_buffer_map.span();
    std::size_t offset      {0};
    uint16_t    vertex_index{0};
    for (unsigned int i = 0; i < max_quad_count; ++i) {
        gpu_index_data[offset + 0] = vertex_index;
        gpu_index_data[offset + 1] = vertex_index + 1;
        gpu_index_data[offset + 2] = vertex_index + 2;
        gpu_index_data[offset + 3] = vertex_index + 3;
        gpu_index_data[offset + 4] = uint16_primitive_restart;
        vertex_index += 4;
        offset += 5;
    }

    m_font = std::make_unique<erhe::ui::Font>(
        m_graphics_instance,
        "res/fonts/SourceSansPro-Regular.otf",
        config.font_size,
        0.0f // TODO reimplement outline better 1.0f
    );

    {
        ERHE_PROFILE_SCOPE("shader");

        const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
        const std::filesystem::path vs_path = shader_path / std::filesystem::path("text.vert");
        const std::filesystem::path fs_path = shader_path / std::filesystem::path("text.frag");
        erhe::graphics::Shader_stages_create_info create_info{
            .name                      = "text",
            .interface_blocks          = { &m_projection_block },
            .vertex_attribute_mappings = &m_attribute_mappings,
            .fragment_outputs          = &m_fragment_outputs,
            .shaders = {
                { igl::ShaderStage::vertex_shader,   vs_path },
                { igl::ShaderStage::fragment_shader, fs_path }
            }
        };

        if (graphics_instance.info.use_bindless_texture) {
            create_info.extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_bindless_texture"});
            create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        } else {
            m_default_uniform_block.add_sampler(
                "s_texture",
                igl::UniformType::sampler_2d,
                0
            );
            create_info.default_uniform_block = &m_default_uniform_block;
        }

        erhe::graphics::Shader_stages_prototype prototype{graphics_instance, create_info};
        if (!prototype.is_valid()) {
            log_startup->error("Text renderer shader compilation failed");
            config.enabled = false;
            m_font.reset();
            return;
        }

        m_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(std::move(prototype));
        graphics_instance.shader_monitor.add(create_info, m_shader_stages.get());
    }

    create_frame_resources();
}

void Text_renderer::create_frame_resources()
{
    ERHE_PROFILE_FUNCTION();

    constexpr std::size_t vertex_count{65536 * 8};
    const bool reverse_depth = m_graphics_instance.configuration.reverse_depth;
    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        m_frame_resources.emplace_back(
            m_graphics_instance,
            reverse_depth,
            vertex_count,
            m_shader_stages.get(),
            m_attribute_mappings,
            m_vertex_format,
            m_index_buffer,
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
    const std::string_view text
)
{
    ERHE_PROFILE_FUNCTION();

    if (!config.enabled || !m_font) {
        return;
    }

    auto* const               vertex_buffer     = &current_frame_resources().vertex_buffer;
    const std::size_t         quad_count        = m_font->get_glyph_count(text);
    const std::size_t         vertex_byte_count = quad_count * 4 * m_vertex_format.stride();
    const auto                vertex_gpu_data   = m_vertex_writer.begin(vertex_buffer, vertex_byte_count);
    std::byte* const          start             = vertex_gpu_data.data();
    const std::size_t         byte_count        = vertex_gpu_data.size_bytes();
    const std::size_t         word_count        = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    erhe::ui::Rectangle bounding_box;
    const vec3          snapped_position{
        std::floor(text_position.x + 0.5f),
        std::floor(text_position.y + 0.5f),
        text_position.z
    };
    const std::size_t quad_count2 = m_font->print(
        gpu_float_data,
        gpu_uint_data,
        text,
        snapped_position,
        text_color,
        bounding_box
    );
    ERHE_VERIFY(quad_count2 == quad_count);
    m_vertex_writer.write_offset += vertex_byte_count; // quad_count * 4 * m_vertex_format.stride();
    m_vertex_writer.end();
    m_index_count += quad_count * 5;
}

auto Text_renderer::font_size() -> float
{
    return static_cast<float>(config.font_size);
}

auto Text_renderer::measure(const std::string_view text) const -> erhe::ui::Rectangle
{
    return m_font
        ? m_font->measure(text)
        : erhe::ui::Rectangle{};
}

static constexpr std::string_view c_text_renderer_render{"Text_renderer::render()"};

void Text_renderer::render(
    erhe::math::Viewport viewport
)
{
    if (m_index_count == 0) {
        return;
    }

    ERHE_PROFILE_FUNCTION();

    //ERHE_PROFILE_GPU_SCOPE(c_text_renderer_render)

    const auto handle = m_graphics_instance.get_handle(
        *m_font->texture().get(),
        m_nearest_sampler
    );

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_render};

    auto* const               projection_buffer   = &current_frame_resources().projection_buffer;
    const auto                projection_gpu_data = m_projection_writer.begin(projection_buffer, m_projection_block.size_bytes());
    std::byte* const          start               = projection_gpu_data.data();
    const std::size_t         byte_count          = projection_gpu_data.size_bytes();
    const std::size_t         word_count          = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const mat4 clip_from_window = erhe::math::create_orthographic(
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
        gl::bind_texture_unit(0, m_font->texture()->gl_name());
        gl::bind_sampler(0, m_nearest_sampler.gl_name());
    }

    gl::draw_elements(
        pipeline.data.input_assembly.primitive_topology,
        static_cast<GLsizei>(m_index_count),
        gl::Draw_elements_type::unsigned_short,
        reinterpret_cast<const void*>(m_index_range_first * 2)
    );

    if (m_graphics_instance.info.use_bindless_texture) {
        gl::make_texture_handle_non_resident_arb(handle);
    }

    gl::disable(gl::Enable_cap::primitive_restart_fixed_index);

    m_index_range_first += m_index_count;
    m_index_count = 0;
}

} // namespace erhe::renderer
