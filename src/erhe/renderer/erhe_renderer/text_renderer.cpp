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
        .interface_blocks = { &m_projection_block, &m_vertex_ssbo_block },
        .fragment_outputs = &m_fragment_outputs,
        .vertex_format    = nullptr,
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
    , m_vertex_ssbo_block        {graphics_instance, "vertex_ssbo", 1, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , m_clip_from_window_resource{m_projection_block.add_mat4 ("clip_from_window")}
    , m_texture_resource         {m_projection_block.add_uvec2("texture")}
    , m_vertex_data_resource     {m_vertex_ssbo_block.add_uvec4("data", erhe::graphics::Shader_resource::unsized_array)} // x,y | z,0 | color | u,v
    , m_u_clip_from_window_size  {m_clip_from_window_resource->size_bytes()}
    , m_u_clip_from_window_offset{m_clip_from_window_resource->offset_in_parent()}
    , m_u_texture_size           {m_texture_resource->size_bytes()}
    , m_u_texture_offset         {m_texture_resource->offset_in_parent()}
    , m_u_vertex_data_size       {m_vertex_data_resource->size_bytes()}
    , m_u_vertex_data_offset     {m_vertex_data_resource->offset_in_parent()}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , m_nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Text_renderer::m_nearest_sampler"
        }
    }    
    , m_shader_stages{build_shader_stages()} 
    , m_vertex_ssbo_buffer{graphics_instance, "Text_renderer::m_vertex_buffer", gl::Buffer_target::shader_storage_buffer, m_vertex_ssbo_block.binding_point()}
    , m_projection_buffer{graphics_instance, "Text_renderer::m_projection_buffer", gl::Buffer_target::uniform_buffer, m_projection_block.binding_point()}
    , m_vertex_input{{}}
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

    const std::size_t vertex_stride     = m_u_vertex_data_size;
    const std::size_t vertex_byte_count = quad_count_requested * 4 * vertex_stride;
    const std::size_t min_range_size    = 4096;

    if (m_vertex_buffer_ranges.empty())  {
        m_vertex_buffer_ranges.push_back(
            m_vertex_ssbo_buffer.allocate_range(
                erhe::graphics::Ring_buffer_usage::CPU_write,
                std::max(vertex_byte_count, min_range_size)
            )
        );
    }

    if (m_vertex_buffer_ranges.back().get_writable_byte_count() < vertex_byte_count) {
        m_vertex_buffer_ranges.push_back(
            m_vertex_ssbo_buffer.allocate_range(
                erhe::graphics::Ring_buffer_usage::CPU_write,
                std::max(vertex_byte_count, min_range_size)
            )
        );
    }
    erhe::graphics::Buffer_range& vertex_buffer_range = m_vertex_buffer_ranges.back(); //m_vertex_buffer_range.value();
    std::size_t                   span_write_offset   = vertex_buffer_range.get_written_byte_count();
    const auto                    vertex_gpu_data     = vertex_buffer_range.get_span();
    std::byte* const              start               = vertex_gpu_data.data();
    const std::size_t             word_count          = vertex_byte_count / sizeof(float);
    const std::span<uint32_t>     gpu_uint_data {reinterpret_cast<uint32_t*>(start + span_write_offset), word_count};

    erhe::ui::Rectangle bounding_box;
    const vec3          snapped_position{
        std::floor(text_position.x + 0.5f),
        std::floor(text_position.y + 0.5f),
        text_position.z
    };
    const std::size_t quad_count_printed = m_font->print(
        gpu_uint_data,
        text,
        snapped_position,
        text_color,
        bounding_box
    );
    ERHE_VERIFY(quad_count_printed <= quad_count_requested);
    vertex_buffer_range.bytes_written(quad_count_printed * 4 * vertex_stride);
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

    if (m_vertex_buffer_ranges.empty()) {
        return;
    }

    const uint64_t handle = m_graphics_instance.get_handle(*m_font->texture(), m_nearest_sampler);
    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_render};

    erhe::graphics::Buffer_range projection_buffer_range = m_projection_buffer.allocate_range(erhe::graphics::Ring_buffer_usage::CPU_write, m_projection_block.size_bytes());
    {
        const auto                projection_gpu_data = projection_buffer_range.get_span();
        std::byte* const          start               = projection_gpu_data.data();
        const std::size_t         byte_count          = projection_gpu_data.size_bytes();
        const std::size_t         word_count          = byte_count / sizeof(float);
        const std::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
        const std::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

        const mat4 clip_from_window = erhe::math::create_orthographic(
            static_cast<float>(viewport.x), static_cast<float>(viewport.width),
            static_cast<float>(viewport.y), static_cast<float>(viewport.height),
            0.0f,
            1.0f
        );

        const uint32_t texture_handle[2] = {
            static_cast<uint32_t>((handle & 0xffffffffu)),
            static_cast<uint32_t>(handle >> 32u)
        };
        const std::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};

        using erhe::graphics::as_span;
        using erhe::graphics::write;
        write(gpu_float_data,  m_u_clip_from_window_offset, as_span(clip_from_window));
        write(gpu_uint32_data, m_u_texture_offset,          texture_handle_cpu_data);
        projection_buffer_range.bytes_written(m_projection_block.size_bytes());
        projection_buffer_range.close();
        m_projection_buffer.bind(projection_buffer_range);
    }

    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);

    const std::size_t vertex_ssbo_stride = m_u_vertex_data_size;
    const std::size_t bytes_per_quad     = 4 * vertex_ssbo_stride;

    for (erhe::graphics::Buffer_range& vertex_buffer_range : m_vertex_buffer_ranges) {
        vertex_buffer_range.close();

        const erhe::graphics::Buffer* buffer     = vertex_buffer_range.get_buffer()->get_buffer();
        const std::size_t             offset     = vertex_buffer_range.get_byte_start_offset_in_buffer();
        const std::size_t             byte_count = vertex_buffer_range.get_written_byte_count();
        const std::size_t             quad_count = byte_count / bytes_per_quad;

        m_graphics_instance.opengl_state_tracker.execute(m_pipeline);
        m_graphics_instance.opengl_state_tracker.vertex_input.set_vertex_buffer(0, buffer, offset);

        if (m_graphics_instance.info.use_bindless_texture) {
            gl::make_texture_handle_resident_arb(handle);
        } else {
            gl::bind_texture_unit(0, m_font->texture()->gl_name());
            gl::bind_sampler(0, m_nearest_sampler.gl_name());
        }

        gl::draw_arrays(
            m_pipeline.data.input_assembly.primitive_topology,
            0,
            static_cast<GLsizei>(6 * quad_count)
        );

        vertex_buffer_range.submit();
    }

    projection_buffer_range.submit();

    if (m_graphics_instance.info.use_bindless_texture) {
        gl::make_texture_handle_non_resident_arb(handle);
    }

    m_vertex_buffer_ranges.clear();
}

} // namespace erhe::renderer

