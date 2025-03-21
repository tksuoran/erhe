#include "erhe_renderer/text_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_dataformat/vertex_format.hpp"
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
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_ui/font.hpp"

namespace erhe::renderer {

using glm::mat4;
using glm::vec3;
using glm::vec4;


static constexpr std::string_view c_text_renderer_initialize_component{"Text_renderer::initialize_component()"};

constexpr std::size_t s_vertex_count{65536 * 8};

auto Text_renderer::build_shader_stages() -> erhe::graphics::Shader_stages_prototype
{
    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
    const std::filesystem::path vs_path = shader_path / std::filesystem::path("text.vert");
    const std::filesystem::path fs_path = shader_path / std::filesystem::path("text.frag");
    erhe::graphics::Shader_stages_create_info create_info{
        .name             = "text",
        .interface_blocks = { &m_projection_block },
        .fragment_outputs = &m_fragment_outputs,
        .vertex_format    = &m_vertex_format,
        .shaders = {
            { gl::Shader_type::vertex_shader,   vs_path },
            { gl::Shader_type::fragment_shader, fs_path }
        }
    };

    if (m_graphics_instance.info.use_bindless_texture) {
        create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
    } else {
        m_default_uniform_block.add_sampler("s_texture", gl::Uniform_type::sampler_2d, 0);
        create_info.default_uniform_block = &m_default_uniform_block;
    }

    erhe::graphics::Shader_stages_prototype prototype{m_graphics_instance, create_info};
    if (!prototype.is_valid()) {
        log_startup->error("Text renderer shader compilation failed");
        config.enabled = false;
    }

    return prototype;
}

Text_renderer::Text_renderer(erhe::graphics::Instance& graphics_instance)
    : m_graphics_instance        {graphics_instance}
    , m_default_uniform_block    {graphics_instance}
    , m_projection_block         {graphics_instance, "projection", 0, erhe::graphics::Shader_resource::Type::uniform_block}
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
    , m_vertex_format{
        {
            0,
            {
                { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position  },
                { erhe::dataformat::Format::format_8_vec4_unorm,  erhe::dataformat::Vertex_attribute_usage::color     },
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord }
            }
        }
    }
    , m_index_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::element_array_buffer,
            .capacity_byte_count = index_stride * index_count,
            .storage_mask        = gl::Buffer_storage_mask::map_write_bit
        }
    }
    , m_nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Text_renderer"
        }
    }    
    , m_shader_stages{build_shader_stages()} 
    , m_vertex_buffer{
        graphics_instance,
        erhe::renderer::GPU_ring_buffer_create_info{
            .target      = gl::Buffer_target::array_buffer,
            .size        = m_vertex_format.streams.front().stride * s_vertex_count,
            .debug_label = "Text renderer vertex ring buffer"
        }
    }
    , m_projection_buffer{
        graphics_instance,
        erhe::renderer::GPU_ring_buffer_create_info{
            .target        = gl::Buffer_target::uniform_buffer,
            .binding_point = m_projection_block.binding_point(),
            .size          = 1024,
            .debug_label   = "Text renderer projection ring buffer"
        }
    }
    , m_vertex_input{
        erhe::graphics::Vertex_input_state_data::make(m_vertex_format)
    }
    , m_pipeline{
        erhe::graphics::Pipeline_data{
            .name           = "Text renderer",
            .shader_stages  = &m_shader_stages,
            .vertex_input   = &m_vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(graphics_instance.configuration.reverse_depth),
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied,
        }
    }
{
    ERHE_PROFILE_FUNCTION();

    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "text_renderer");
    ini.get("enabled",   config.enabled);
    ini.get("font_size", config.font_size);

    if (!config.enabled) {
        log_startup->info("Text renderer disabled due to erhe.ini setting");
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_initialize_component};

    // Prefill index buffer
    erhe::graphics::Scoped_buffer_mapping<uint16_t> index_buffer_map{m_index_buffer, 0, index_count, gl::Map_buffer_access_mask::map_write_bit};
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

    // Init font
    m_font = std::make_unique<erhe::ui::Font>(
        m_graphics_instance,
        "res/fonts/SourceSansPro-Regular.otf",
        config.font_size,
        0.0f // TODO reimplement outline better 1.0f
    );
}


void Text_renderer::print(const glm::vec3 text_position, const uint32_t text_color, const std::string_view text)
{
    ERHE_PROFILE_FUNCTION();

    if (!config.enabled || !m_font) {
        return;
    }

    const std::size_t quad_count_requested = m_font->get_glyph_count(text);
    if (quad_count_requested == 0) {
        return;
    }

    const std::size_t vertex_stride     = m_vertex_format.streams.front().stride;
    const std::size_t vertex_byte_count = quad_count_requested * 4 * vertex_stride;

    if (!m_vertex_buffer_range.has_value()) {
        const std::size_t total_capacity_byte_count = m_vertex_buffer.get_buffer().capacity_byte_count();
        const std::size_t reserved_byte_count = total_capacity_byte_count / 8;
        m_vertex_buffer_range = m_vertex_buffer.open(Ring_buffer_usage::CPU_write, reserved_byte_count);
        m_vertex_write_offset = 0;
    }
    Buffer_range&             vertex_buffer_range = m_vertex_buffer_range.value();
    const auto                vertex_gpu_data     = vertex_buffer_range.get_span();
    std::byte* const          start               = vertex_gpu_data.data();
    const std::size_t         byte_count          = std::min(vertex_gpu_data.size_bytes() - m_vertex_write_offset, vertex_byte_count);
    const std::size_t         word_count          = byte_count / sizeof(float);
    const std::span<float>    gpu_float_data{reinterpret_cast<float*   >(start + m_vertex_write_offset), word_count};
    const std::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start + m_vertex_write_offset), word_count};

    erhe::ui::Rectangle bounding_box;
    const vec3          snapped_position{
        std::floor(text_position.x + 0.5f),
        std::floor(text_position.y + 0.5f),
        text_position.z
    };
    const std::size_t quad_count_printed = m_font->print(
        gpu_float_data,
        gpu_uint_data,
        text,
        snapped_position,
        text_color,
        bounding_box
    );
    ERHE_VERIFY(quad_count_printed <= quad_count_requested);
    m_vertex_write_offset += quad_count_printed * 4 * vertex_stride;
    m_index_count += quad_count_printed * 5;
}

auto Text_renderer::font_size() -> float
{
    return static_cast<float>(config.font_size);
}

auto Text_renderer::measure(const std::string_view text) const -> erhe::ui::Rectangle
{
    return m_font ? m_font->measure(text) : erhe::ui::Rectangle{};
}

static constexpr std::string_view c_text_renderer_render{"Text_renderer::render()"};

void Text_renderer::render(erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    if (m_index_count == 0) {
        return;
    }
    if (!m_vertex_buffer_range.has_value()) {
        return;
    }
    if (m_vertex_write_offset == 0) {
        return;
    }

    Buffer_range& vertex_buffer_range = m_vertex_buffer_range.value();
    vertex_buffer_range.close(m_vertex_write_offset);

    const uint64_t handle = m_graphics_instance.get_handle(*m_font->texture(), m_nearest_sampler);

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_render};

    Buffer_range              projection_buffer_range = m_projection_buffer.open(Ring_buffer_usage::CPU_write, m_projection_block.size_bytes());
    const auto                projection_gpu_data     = projection_buffer_range.get_span();
    size_t                    write_offset            = 0;
    std::byte* const          start                   = projection_gpu_data.data();
    const std::size_t         byte_count              = projection_gpu_data.size_bytes();
    const std::size_t         word_count              = byte_count / sizeof(float);
    const std::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const mat4 clip_from_window = erhe::math::create_orthographic(
        static_cast<float>(viewport.x), static_cast<float>(viewport.width),
        static_cast<float>(viewport.y), static_cast<float>(viewport.height),
        0.0f,
        1.0f
    );
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    write(gpu_float_data, m_u_clip_from_window_offset, as_span(clip_from_window));
    write_offset += m_u_clip_from_window_size;

    const uint32_t texture_handle[2] = {
        static_cast<uint32_t>((handle & 0xffffffffu)),
        static_cast<uint32_t>(handle >> 32u)
    };
    const std::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};
    write(gpu_uint32_data, m_u_texture_offset, texture_handle_cpu_data);
    write_offset += m_u_texture_size;

    projection_buffer_range.close(write_offset);

    const std::size_t vertex_offset = vertex_buffer_range.get_byte_start_offset_in_buffer();

    gl::enable  (gl::Enable_cap::primitive_restart_fixed_index);
    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    m_graphics_instance.opengl_state_tracker.execute(m_pipeline);
    m_graphics_instance.opengl_state_tracker.vertex_input.set_index_buffer(&m_index_buffer);
    m_graphics_instance.opengl_state_tracker.vertex_input.set_vertex_buffer(0, &m_vertex_buffer.get_buffer(), vertex_offset);

    projection_buffer_range.bind();

    if (m_graphics_instance.info.use_bindless_texture) {
        gl::make_texture_handle_resident_arb(handle);
    } else {
        gl::bind_texture_unit(0, m_font->texture()->gl_name());
        gl::bind_sampler(0, m_nearest_sampler.gl_name());
    }

    gl::draw_elements(
        m_pipeline.data.input_assembly.primitive_topology,
        static_cast<GLsizei>(m_index_count),
        gl::Draw_elements_type::unsigned_short,
        0 //reinterpret_cast<const void*>(index_offset * index_stride)
    );

    vertex_buffer_range.submit();
    projection_buffer_range.submit();

    m_vertex_buffer_range.reset();

    if (m_graphics_instance.info.use_bindless_texture) {
        gl::make_texture_handle_non_resident_arb(handle);
    }

    gl::disable(gl::Enable_cap::primitive_restart_fixed_index);

    m_index_count = 0;
}

} // namespace erhe::renderer
