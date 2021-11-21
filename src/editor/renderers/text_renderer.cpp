#include "renderers/text_renderer.hpp"
#include "graphics/gl_context_provider.hpp"
#include "log.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/scoped_buffer_mapping.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/ui/font.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iomanip>
#include <cstdarg>

namespace editor
{

using namespace erhe::graphics;
using namespace erhe::scene;
using namespace erhe::ui;
using namespace gl;
using namespace std;

using glm::mat4;
using glm::vec3;
using glm::vec4;

Text_renderer::Text_renderer()
    : Component(c_name)
{
    ZoneScoped;
}

Text_renderer::~Text_renderer() = default;

void Text_renderer::connect()
{
    require<Gl_context_provider>();

    m_pipeline_state_tracker = get<OpenGL_state_tracker>();
}

static constexpr std::string_view c_text_renderer_initialize_component{"Text_renderer::initialize_component()"};
void Text_renderer::initialize_component()
{
    ZoneScoped;

    Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_text_renderer_initialize_component.length()),
        c_text_renderer_initialize_component.data()
    );

    m_projection_block = std::make_unique<erhe::graphics::Shader_resource>("projection", 0, erhe::graphics::Shader_resource::Type::uniform_block);

    constexpr gl::Buffer_storage_mask    storage_mask  {gl::Buffer_storage_mask::map_write_bit};
    constexpr gl::Map_buffer_access_mask access_mask   {gl::Map_buffer_access_mask::map_write_bit};
    constexpr size_t                     max_quad_count{65536 / 4}; // each quad consumes 4 indices
    constexpr size_t                     index_count   {65536 * 5};
    constexpr size_t                     index_stride  {2};

    m_index_buffer = std::make_unique<erhe::graphics::Buffer>(
        gl::Buffer_target::element_array_buffer,
        index_stride * index_count,
        storage_mask
    );

    erhe::graphics::Scoped_buffer_mapping<uint16_t> index_buffer_map(*m_index_buffer.get(), 0, index_count, access_mask);
    auto     gpu_index_data = index_buffer_map.span();
    size_t   offset      {0};
    uint16_t vertex_index{0};
    for (unsigned int i = 0; i < max_quad_count; ++i)
    {
        gpu_index_data[offset + 0] = vertex_index;
        gpu_index_data[offset + 1] = vertex_index + 1;
        gpu_index_data[offset + 2] = vertex_index + 2;
        gpu_index_data[offset + 3] = vertex_index + 3;
        gpu_index_data[offset + 4] = 0xffffu;
        vertex_index += 4;
        offset += 5;
    }

    m_fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    m_attribute_mappings.add(gl::Attribute_type::float_vec3, "a_position", {erhe::graphics::Vertex_attribute::Usage_type::position,  0}, 0);
    m_attribute_mappings.add(gl::Attribute_type::float_vec4, "a_color",    {erhe::graphics::Vertex_attribute::Usage_type::color,     0}, 1);
    m_attribute_mappings.add(gl::Attribute_type::float_vec2, "a_texcoord", {erhe::graphics::Vertex_attribute::Usage_type::tex_coord, 0}, 2);

    m_vertex_format.make_attribute(
        {erhe::graphics::Vertex_attribute::Usage_type::position, 0},
        gl::Attribute_type::float_vec3,
        {gl::Vertex_attrib_type::float_, false, 3}
    );
    m_vertex_format.make_attribute(
        {erhe::graphics::Vertex_attribute::Usage_type::color, 0},
        gl::Attribute_type::float_vec4,
        {gl::Vertex_attrib_type::unsigned_byte, true, 4}
    );
    m_vertex_format.make_attribute(
        {erhe::graphics::Vertex_attribute::Usage_type::tex_coord, 0},
        gl::Attribute_type::float_vec2,
        {gl::Vertex_attrib_type::float_, false, 2}
    );

    m_nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::nearest,
        gl::Texture_mag_filter::nearest
    );

    m_font_sampler_location = m_default_uniform_block.add_sampler("s_texture", gl::Uniform_type::sampler_2d)->location();

    m_projection_block->add_mat4("clip_from_window");

    m_font = std::make_unique<erhe::ui::Font>("res/fonts/Ubuntu-R.ttf", 12, 0.4f);

    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
    const std::filesystem::path vs_path = shader_path / std::filesystem::path("text.vert");
    const std::filesystem::path fs_path = shader_path / std::filesystem::path("text.frag");
    Shader_stages::Create_info create_info{
        "stream",
        &m_default_uniform_block,
        &m_attribute_mappings,
        &m_fragment_outputs
    };
    create_info.add_interface_block(m_projection_block.get());
    create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    Shader_stages::Prototype prototype(create_info);
    m_shader_stages = std::make_unique<Shader_stages>(std::move(prototype));

    create_frame_resources();

    gl::pop_debug_group();
}

void Text_renderer::create_frame_resources()
{
    ZoneScoped;

    constexpr size_t vertex_count = 65536;
    for (size_t i = 0; i < s_frame_resources_count; ++i)
    {
        m_frame_resources.emplace_back(
            vertex_count,
            m_shader_stages.get(),
            m_attribute_mappings,
            m_vertex_format,
            m_index_buffer.get()
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
    m_vertex_writer.reset();
    m_quad_count = 0;
}

void Text_renderer::print(
    const glm::vec3  text_position,
    const uint32_t   text_color,
    std::string_view text)
{
    ZoneScoped;

    auto vertex_gpu_data = current_frame_resources().vertex_buffer.map();

    m_vertex_writer.begin();

    std::byte* const    start      = vertex_gpu_data.data() + m_vertex_writer.write_offset;
    const size_t        byte_count = vertex_gpu_data.size_bytes();
    const size_t        word_count = byte_count / sizeof(float);
    gsl::span<float>    gpu_float_data(reinterpret_cast<float* const   >(start), word_count);
    gsl::span<uint32_t> gpu_uint_data (reinterpret_cast<uint32_t* const>(start), word_count);

    erhe::ui::Rectangle bounding_box;
    const size_t quad_count = m_font->print(
        gpu_float_data,
        gpu_uint_data,
        text,
        text_position,
        text_color,
        bounding_box
    );
    m_vertex_writer.write_offset += quad_count * 4 * m_vertex_format.stride();
    m_quad_count += quad_count;
    m_vertex_writer.end();
}

static constexpr std::string_view c_text_renderer_render{"Text_renderer::render()"};
void Text_renderer::render(erhe::scene::Viewport viewport)
{
    if (m_quad_count == 0)
    {
        return;
    }

    ZoneScoped;
    TracyGpuZone(c_text_renderer_render.data())

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_text_renderer_render.length()),
        c_text_renderer_render.data()
    );

    const mat4 clip_from_window = erhe::toolkit::create_orthographic(
        static_cast<float>(viewport.x), static_cast<float>(viewport.width),
        static_cast<float>(viewport.y), static_cast<float>(viewport.height),
        0.0f,
        1.0f
    );
    auto* const projection_buffer   = &current_frame_resources().projection_buffer;
    auto        projection_gpu_data = projection_buffer->map();
    write(projection_gpu_data, 0, as_span(clip_from_window));

    const auto& pipeline = current_frame_resources().pipeline;

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(&erhe::graphics::Color_blend_state::color_blend_disabled);
    gl::disable (gl::Enable_cap::framebuffer_srgb);
    gl::enable  (gl::Enable_cap::primitive_restart_fixed_index);
    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    m_pipeline_state_tracker->execute(&pipeline);

    const unsigned int font_texture_unit = 0;
    const unsigned int font_texture_name = m_font->texture()->gl_name();
    gl::program_uniform_1i(
        pipeline.shader_stages->gl_name(),
        m_font_sampler_location,
        font_texture_unit
    );
    gl::bind_sampler(font_texture_unit, m_nearest_sampler->gl_name());
    gl::bind_textures(font_texture_unit, 1, &font_texture_name);

    gl::bind_buffer_range(
        projection_buffer->target(),
        static_cast<GLuint>    (m_projection_block->binding_point()),
        static_cast<GLuint>    (projection_buffer->gl_name()),
        static_cast<GLintptr>  (0),
        static_cast<GLsizeiptr>(4 * 4 * sizeof(float))
    );

    const size_t index_count = 5 * m_quad_count;
    gl::draw_elements(
        pipeline.input_assembly->primitive_topology,
        static_cast<GLsizei>(index_count),
        gl::Draw_elements_type::unsigned_short,
        nullptr
    );
    gl::disable(gl::Enable_cap::primitive_restart_fixed_index);

    gl::pop_debug_group();
}


}
