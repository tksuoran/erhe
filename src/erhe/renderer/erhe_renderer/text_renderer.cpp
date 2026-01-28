#include "erhe_renderer/text_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_configuration/configuration.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_ui/font.hpp"

namespace erhe::renderer {

static constexpr std::string_view c_text_renderer_initialize_component{"Text_renderer::initialize_component()"};

auto Text_renderer::build_shader_stages() -> erhe::graphics::Shader_stages_prototype
{
    using namespace erhe::graphics;

    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
    const std::filesystem::path vs_path = shader_path / std::filesystem::path("text.vert");
    const std::filesystem::path fs_path = shader_path / std::filesystem::path("text.frag");
    Shader_stages_create_info create_info{
        .name             = "text",
        .interface_blocks = { &m_projection_block, &m_vertex_ssbo_block },
        .fragment_outputs = &m_fragment_outputs,
        .vertex_format    = nullptr,
        .shaders = {
            { Shader_type::vertex_shader,   vs_path },
            { Shader_type::fragment_shader, fs_path }
        }
    };

    if (!m_graphics_device.get_info().use_bindless_texture) {
        m_default_uniform_block.add_sampler("s_texture", erhe::graphics::Glsl_type::sampler_2d, 0);
        create_info.default_uniform_block = &m_default_uniform_block;
    }

    Shader_stages_prototype prototype{m_graphics_device, create_info};
    if (!prototype.is_valid()) {
        log_startup->error("Text renderer shader compilation failed");
        config.enabled = false;
    }

    return prototype;
}

Text_renderer::Text_renderer(erhe::graphics::Device& graphics_device)
    : m_graphics_device          {graphics_device}
    , m_default_uniform_block    {graphics_device}
    , m_projection_block         {graphics_device, "projection", 0, erhe::graphics::Shader_resource::Type::uniform_block}
    , m_vertex_ssbo_block        {graphics_device, "vertex_ssbo", 1, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , m_clip_from_window_resource{m_projection_block.add_mat4 ("clip_from_window")}
    , m_texture_resource         {m_projection_block.add_uvec2("texture")}
    , m_vertex_data_resource     {m_vertex_ssbo_block.add_uvec4("data", erhe::graphics::Shader_resource::unsized_array)} // x,y | z,w | color | u,v
    , m_u_clip_from_window_size  {m_clip_from_window_resource->get_size_bytes()}
    , m_u_clip_from_window_offset{m_clip_from_window_resource->get_offset_in_parent()}
    , m_u_texture_size           {m_texture_resource         ->get_size_bytes()}
    , m_u_texture_offset         {m_texture_resource         ->get_offset_in_parent()}
    , m_u_vertex_data_size       {m_vertex_data_resource     ->get_size_bytes()}
    , m_u_vertex_data_offset     {m_vertex_data_resource     ->get_offset_in_parent()}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , m_nearest_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Text_renderer::m_nearest_sampler"
        }
    }
    , m_shader_stages     {graphics_device, build_shader_stages()}
    , m_vertex_ssbo_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Text_renderer::m_vertex_buffer",
        m_vertex_ssbo_block.get_binding_point()
    }
    , m_projection_buffer {
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Text_renderer::m_projection_buffer",
        m_projection_block .get_binding_point()
    }
    , m_vertex_input      {graphics_device, {}}
    , m_pipeline{
        erhe::graphics::Render_pipeline_data{
            .name           = "Text renderer",
            .shader_stages  = &m_shader_stages,
            .vertex_input   = &m_vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied,
        }
    }
{
    ERHE_PROFILE_FUNCTION();

    const auto& ini = erhe::configuration::get_ini_file_section(c_erhe_config_file_path, "text_renderer");
    ini.get("enabled",   config.enabled);
    ini.get("font_size", config.font_size);

    if (!config.enabled) {
        log_startup->info("Text renderer disabled due to erhe.toml setting");
        return;
    }

    // Init font
    m_font = std::make_unique<erhe::ui::Font>(
        m_graphics_device,
        "res/fonts/SourceSansPro-Regular.otf",
        config.font_size,
        0.0f // TODO reimplement outline better 1.0f
    );
}

Text_renderer::~Text_renderer() noexcept
{
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
            m_vertex_ssbo_buffer.acquire(
                erhe::graphics::Ring_buffer_usage::CPU_write,
                std::max(vertex_byte_count, min_range_size)
            )
        );
    }

    if (m_vertex_buffer_ranges.back().get_writable_byte_count() < vertex_byte_count) {
        m_vertex_buffer_ranges.push_back(
            m_vertex_ssbo_buffer.acquire(
                erhe::graphics::Ring_buffer_usage::CPU_write,
                std::max(vertex_byte_count, min_range_size)
            )
        );
    }
    erhe::graphics::Ring_buffer_range& vertex_buffer_range = m_vertex_buffer_ranges.back();
    std::size_t                        span_write_offset   = vertex_buffer_range.get_written_byte_count();
    const auto                         vertex_gpu_data     = vertex_buffer_range.get_span();
    std::byte* const                   start               = vertex_gpu_data.data();
    const std::size_t                  word_count          = vertex_byte_count / sizeof(float);
    const std::span<uint32_t>          gpu_uint_data {reinterpret_cast<uint32_t*>(start + span_write_offset), word_count};

    erhe::ui::Rectangle bounding_box;
    const glm::vec3     snapped_position{
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

void Text_renderer::render(erhe::graphics::Render_command_encoder& encoder, erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    if (m_vertex_buffer_ranges.empty()) {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{"Text_renderer::render()"};

    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device,
        *m_font->texture(),
        m_nearest_sampler,
        0
    };

    erhe::graphics::Ring_buffer_range projection_buffer_range = m_projection_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, m_projection_block.get_size_bytes());
    {
        const std::span<std::byte> gpu_data = projection_buffer_range.get_span();
        const glm::mat4 clip_from_window = erhe::math::create_orthographic(
            static_cast<float>(viewport.x), static_cast<float>(viewport.width),
            static_cast<float>(viewport.y), static_cast<float>(viewport.height),
            0.0f,
            1.0f
        );

        const uint64_t shader_handle = texture_heap.allocate(m_font->texture(), &m_nearest_sampler);

        using erhe::graphics::as_span;
        using erhe::graphics::write;
        write(gpu_data, m_u_clip_from_window_offset, as_span(clip_from_window));
        write(gpu_data, m_u_texture_offset,          as_span(shader_handle));
        projection_buffer_range.bytes_written(m_projection_block.get_size_bytes());
        projection_buffer_range.close();
        m_projection_buffer.bind(encoder, projection_buffer_range);
    }

    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    encoder.set_render_pipeline_state(m_pipeline);
    texture_heap.bind();

    const std::size_t vertex_ssbo_stride = m_u_vertex_data_size;
    const std::size_t bytes_per_quad     = 4 * vertex_ssbo_stride;

    for (erhe::graphics::Ring_buffer_range& vertex_buffer_range : m_vertex_buffer_ranges) {
        vertex_buffer_range.close();

        const std::size_t byte_count = vertex_buffer_range.get_written_byte_count();
        const std::size_t quad_count = byte_count / bytes_per_quad;

        m_vertex_ssbo_buffer.bind(encoder, vertex_buffer_range);

        encoder.draw_primitives(
            m_pipeline.data.input_assembly.primitive_topology,
            0,
            6 * quad_count
        );

        vertex_buffer_range.release();
    }

    projection_buffer_range.release();

    texture_heap.unbind();

    m_vertex_buffer_ranges.clear();
}

} // namespace erhe::renderer

