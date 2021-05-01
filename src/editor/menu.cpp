#include "menu.hpp"

#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/span.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"

#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/window.hpp"

#include "erhe/ui/font.hpp"
#include "erhe/ui/style.hpp"
#include "erhe/ui/text_buffer.hpp"

#include "application.hpp"
#include "log.hpp"
#include "programs.hpp"
#include "textures.hpp"

#include <cmath>

namespace sample {

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using erhe::graphics::as_span;
using erhe::graphics::Buffer;
using erhe::graphics::Framebuffer;
using erhe::graphics::Multi_buffer;
using erhe::graphics::Shader_stages;
using erhe::graphics::Sampler;
using erhe::graphics::Scoped_buffer_mapping;
using erhe::graphics::Texture;
using erhe::graphics::Vertex_attribute;
using erhe::graphics::Vertex_input_state;
using erhe::ui::Font;
using erhe::primitive::Mesh;
using erhe::log::Log;
using std::make_shared;
using std::make_unique;
using std::shared_ptr;

Menu::Menu()
    : erhe::components::Component{"Menu"}
    , m_resize{true}
{
}

void Menu::connect(shared_ptr<Application>                          application,
                   std::shared_ptr<erhe::graphics::Command_buffer>  command_buffer,
                   shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker,
                   shared_ptr<Programs>                             programs,
                   shared_ptr<Textures>                             textures)
{
    m_application            = application;
    m_command_buffer         = command_buffer;
    m_pipeline_state_tracker = pipeline_state_tracker;
    m_programs               = programs;
    m_textures               = textures;

    initialization_depends_on(command_buffer);
    initialization_depends_on(pipeline_state_tracker);
    initialization_depends_on(programs);
    initialization_depends_on(textures);
}

void Menu::disconnect()
{
    m_application           .reset();
    m_command_buffer        .reset();
    m_pipeline_state_tracker.reset();
    m_programs              .reset();
    m_textures              .reset();
}

void Menu::initialize_background()
{
    Expects(m_vertex_buffer);
    Expects(m_index_buffer);

    m_background_mesh = make_unique<Mesh>();
    m_background_mesh->allocate_vertex_buffer(m_vertex_buffer, 4);

    m_background_draw.count          = 4;
    m_background_draw.instance_count = 1;
    m_background_draw.first          = static_cast<uint32_t>(m_background_mesh->vertex_offset());
    m_background_draw.base_instance  = 0;

    Ensures(m_background_mesh);
}

void Menu::initialize_component()
{
    intptr_t size{0};
    size += m_programs->models_block   .size_bytes();
    size += m_programs->materials_block.size_bytes() * 2;

    m_uniform_buffers = make_unique<Multi_buffer>(gl::Buffer_target::uniform_buffer, size, 1);

    m_draw_indirect_buffers = make_unique<Multi_buffer>(gl::Buffer_target::draw_indirect_buffer, size, 1024);

    m_nearest_sampler = make_unique<Sampler>(gl::Texture_min_filter::nearest,
                                             gl::Texture_mag_filter::nearest);

    m_linear_sampler = make_unique<Sampler>(gl::Texture_min_filter::linear,
                                            gl::Texture_mag_filter::linear);

    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec4,
                                    "a_position_texcoord",
                                    {Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord, 0},
                                    0);

    m_vertex_format.make_attribute({Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord, 0},
                                   gl::Attribute_type::float_vec4,
                                   {gl::Vertex_attrib_type::float_, false, 4});

    m_vertex_buffer = make_shared<Buffer>(gl::Buffer_target::array_buffer,
                                          40 * 1024,
                                          4 * sizeof(float),
                                          gl::Buffer_storage_mask::client_storage_bit |
                                          gl::Buffer_storage_mask::map_persistent_bit |
                                          gl::Buffer_storage_mask::map_write_bit);

    m_index_buffer = make_shared<Buffer>(gl::Buffer_target::element_array_buffer,
                                         40 * 1024,
                                         sizeof(uint16_t),
                                         gl::Buffer_storage_mask::client_storage_bit |
                                         gl::Buffer_storage_mask::map_persistent_bit |
                                         gl::Buffer_storage_mask::map_write_bit);

    m_vertex_input_state = make_unique<Vertex_input_state>(m_vertex_attribute_mappings,
                                                           m_vertex_format,
                                                           m_vertex_buffer.get(),
                                                           m_index_buffer.get());

    auto vertex_input = m_vertex_input_state.get();

    initialize_background();

    m_font        = make_unique<Font>(std::filesystem::path("res/fonts/Ubuntu-R.ttf"), 100, 20.0f);
    m_text_style  = make_unique<erhe::ui::Style>(m_font.get());
    m_text_buffer = make_unique<erhe::ui::Text_buffer>(m_vertex_buffer, m_index_buffer, m_text_style.get());

    m_background_pipeline.shader_stages  = m_programs->textured.get();
    m_background_pipeline.vertex_input   = m_vertex_input_state.get();
    m_background_pipeline.input_assembly = &erhe::graphics::Input_assembly_state::triangle_fan;
    m_background_pipeline.rasterization  = &erhe::graphics::Rasterization_state::cull_mode_none;
    m_background_pipeline.depth_stencil  = &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled;
    m_background_pipeline.color_blend    = &erhe::graphics::Color_blend_state::color_blend_disabled;
    m_background_pipeline.viewport       = &m_viewport;

    // RGB untouched, alpha = max(src, dst)
    m_font_first_pass_color_blend.enabled                  = true;
    m_font_first_pass_color_blend.rgb.equation_mode        = gl::Blend_equation_mode::func_add;
    m_font_first_pass_color_blend.rgb.source_factor        = gl::Blending_factor::one; // shader writes zero
    m_font_first_pass_color_blend.rgb.destination_factor   = gl::Blending_factor::one;
    m_font_first_pass_color_blend.alpha.equation_mode      = gl::Blend_equation_mode::max_;
    m_font_first_pass_color_blend.alpha.source_factor      = gl::Blending_factor::one;
    m_font_first_pass_color_blend.alpha.destination_factor = gl::Blending_factor::one;

    m_font_first_pass_pipeline.shader_stages  = m_programs->font_first_pass.get();
    m_font_first_pass_pipeline.vertex_input   = m_vertex_input_state.get();
    m_font_first_pass_pipeline.input_assembly = &erhe::graphics::Input_assembly_state::triangle_fan;
    m_font_first_pass_pipeline.rasterization  = &erhe::graphics::Rasterization_state::cull_mode_none;
    m_font_first_pass_pipeline.depth_stencil  = &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled;
    m_font_first_pass_pipeline.color_blend    = &m_font_first_pass_color_blend;
    m_font_first_pass_pipeline.viewport       = &m_viewport;

    m_font_second_pass_color_blend.enabled                  = true;
    m_font_second_pass_color_blend.rgb.equation_mode        = gl::Blend_equation_mode::func_add;
    m_font_second_pass_color_blend.rgb.source_factor        = gl::Blending_factor::one;
    m_font_second_pass_color_blend.rgb.destination_factor   = gl::Blending_factor::one_minus_dst_alpha;
    m_font_second_pass_color_blend.alpha.equation_mode      = gl::Blend_equation_mode::func_add;
    m_font_second_pass_color_blend.alpha.source_factor      = gl::Blending_factor::zero;
    m_font_second_pass_color_blend.alpha.destination_factor = gl::Blending_factor::zero;

    m_font_second_pass_pipeline.shader_stages  = m_programs->font_second_pass.get();
    m_font_second_pass_pipeline.vertex_input   = m_vertex_input_state.get();
    m_font_second_pass_pipeline.input_assembly = &erhe::graphics::Input_assembly_state::triangle_fan;
    m_font_second_pass_pipeline.rasterization  = &erhe::graphics::Rasterization_state::cull_mode_none;
    m_font_second_pass_pipeline.depth_stencil  = &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled;
    m_font_second_pass_pipeline.color_blend    = &m_font_second_pass_color_blend;
    m_font_second_pass_pipeline.viewport       = &m_viewport;

    Expects(m_programs);
    Expects(m_textures);

    Ensures(m_uniform_buffers);
    Ensures(m_vertex_input_state);
    Ensures(m_nearest_sampler);
    Ensures(m_linear_sampler);
}

void Menu::on_resize(int width, int height)
{
    static_cast<void>(width);
    static_cast<void>(height);

    m_resize = true;
}

void Menu::resize(float w, float h)
{
    log_menu.info("{} w = w = {}, h = {}\n", __func__, w, h);

    Expects(m_background_mesh);

    m_viewport = erhe::graphics::Viewport_state{0.0f, 0.0f, w, h, 1.0f, 0.0f};

    {
        // Write corner vertices for one quad
        Scoped_buffer_mapping<float> vertex_map(*m_vertex_buffer.get(),
                                                m_background_mesh->vertex_offset(),
                                                m_background_mesh->vertex_count(),
                                                gl::Map_buffer_access_mask::map_write_bit |
                                                gl::Map_buffer_access_mask::map_invalidate_range_bit);
        auto vertex_data = vertex_map.span();
        float max_x = w;
        float max_y = h;
        size_t offset{0};
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = max_x;
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = 1.0f;
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = max_x;
        vertex_data[offset++] = max_y;
        vertex_data[offset++] = 1.0f;
        vertex_data[offset++] = 1.0f;
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = max_y;
        vertex_data[offset++] = 0.0f;
        vertex_data[offset++] = 1.0f;
    }

    {
        //std::string title = fmt::format("w = {} h = {}", w, h);
        std::string title = "Hello, World!";
        float x = floor(w / 2.0f);
        float y = ceil(3.0f * h / 4.0f);

        m_text_buffer->measure(title);
        glm::vec2 p(x, y);
        p -= m_text_buffer->bounding_box().half_size();

        m_text_buffer->begin_print();
        m_text_buffer->print(title, static_cast<int>(p.x), static_cast<int>(p.y));
        m_text_buffer->end_print();
    }

    m_resize = false;
}

void Menu::update()
{
    render();
}

void Menu::on_enter()
{
    gl::clear_color  (0.3f, 0.2f, 0.4f, 0.0f);
    gl::clear_depth_f(1.0f);
    gl::clip_control (gl::Clip_control_origin::upper_left,
                      gl::Clip_control_depth::zero_to_one);
    gl::clear_stencil(0);
    gl::disable(gl::Enable_cap::scissor_test);
    auto* surface = m_application->get_context_window();
    VERIFY(surface != nullptr);
    on_resize(surface->get_width(), surface->get_height());
}

void Menu::render()
{
    Expects(m_application);
    Expects(m_programs);
    Expects(m_uniform_buffers);
    Expects(m_background_mesh);

    auto& cmd     = *(m_command_buffer.get());
    auto* surface = m_application->get_context_window();
    VERIFY(surface != nullptr);
    int   iw      = surface->get_width();
    int   ih      = surface->get_height();
    float w       = static_cast<float>(iw);
    float h       = static_cast<float>(ih);

    if (m_resize)
    {
        resize(w, h);
    }

    mat4 ortho = glm::orthoLH_ZO(0.0f, w, h, 0.0f, 1.0f, 0.0f);
    vec4 transparent_white(1.0f, 1.0f, 1.0f, 0.0f);

    static auto t_start = std::chrono::steady_clock::now();
    auto t_now = std::chrono::steady_clock::now();
    std::chrono::duration<float> t_diff = t_now - t_start;
    float time  = t_diff.count();
    float ipart;
    float fract = modff(time * 2.5f, &ipart);
    float hue   = fract * 360.0f;
    float s     = 0.5f;
    float v     = 0.5f;
    float r;
    float g;
    float b;
    erhe::toolkit::hsv_to_rgb(hue, s, v, r, g, b);
    vec3 text_color_linear(r, g, b);
    vec4 text_color{erhe::toolkit::linear_rgb_to_srgb(text_color_linear), 1.0f};

    intptr_t models_offset    = 0;
    intptr_t materials_offset = m_programs->models_block.size_bytes();

    auto* uniform_buffer = m_uniform_buffers->current().get();
    auto  uniform_data   = m_uniform_buffers->current_map();

    // Update transformations
    erhe::graphics::write(uniform_data,
                          models_offset + m_programs->models_block_offsets.clip_from_model,
                          as_span(ortho));

    // Update materials - color
    erhe::graphics::write(uniform_data,
                          materials_offset + m_programs->materials_block.size_bytes() * 0 + m_programs->materials_block_offsets.color,
                          as_span(transparent_white));

    erhe::graphics::write(uniform_data,
                          materials_offset + m_programs->materials_block.size_bytes() * 1 + m_programs->materials_block_offsets.color,
                          as_span(text_color));

    // bind models
    cmd.bind_buffer(m_programs->models_block.binding_point(),
                    uniform_buffer,
                    models_offset,
                    m_programs->models_block.size_bytes());

    // bind materials
    cmd.bind_buffer(m_programs->materials_block.binding_point(),
                    uniform_buffer,
                    materials_offset,
                    m_programs->materials_block.size_bytes());

    m_uniform_buffers->advance();

    auto* draw_indirect_buffer = m_draw_indirect_buffers->current().get();
    auto  draw_indirect_data   = m_draw_indirect_buffers->current_map();

    size_t draw_offset{0};

    auto background_draw_offset = draw_offset;
    erhe::graphics::write(draw_indirect_data, draw_offset, as_span(m_background_draw));
    draw_offset += sizeof(gl::Draw_arrays_indirect_command);

    auto text_pass_one_draw_offset = draw_offset;
    erhe::graphics::write(draw_indirect_data, draw_offset, as_span(m_text_buffer->draw()));
    draw_offset += sizeof(gl::Draw_elements_indirect_command);

    auto text_pass_two_draw_offset = draw_offset;
    erhe::graphics::write(draw_indirect_data, draw_offset, as_span(m_text_buffer->draw()));
    //draw_offset += sizeof(gl::Draw_elements_indirect_command);

    m_draw_indirect_buffers->advance();

    gl::clear(gl::Clear_buffer_mask::color_buffer_bit |
              gl::Clear_buffer_mask::depth_buffer_bit |
              gl::Clear_buffer_mask::stencil_buffer_bit);

    cmd.bind_buffer(draw_indirect_buffer);

    // Background
    {
        cmd.bind_pipeline    (&m_background_pipeline);
        cmd.bind_texture_unit(1, m_textures->background_texture.get());
        cmd.bind_sampler     (1, m_linear_sampler.get());
        cmd.draw_arrays      (background_draw_offset);
    }

    // Draw text first pass (alpha max)
    {
        cmd.bind_pipeline    (&m_font_first_pass_pipeline);
        cmd.bind_texture_unit(0, m_font->texture());
        cmd.bind_sampler     (0, m_nearest_sampler.get());
        cmd.draw_elements    (gl::Draw_elements_type::unsigned_short,
                              text_pass_one_draw_offset);
    }

    // Draw text second pass
    {
        cmd.bind_pipeline    (&m_font_second_pass_pipeline);
        cmd.bind_buffer      (m_programs->materials_block.binding_point(),
                              uniform_buffer,
                              materials_offset + m_programs->materials_block.size_bytes() * 1,
                              m_programs->materials_block.size_bytes());
        cmd.bind_texture_unit(0, m_font->texture());
        cmd.bind_sampler     (0, m_nearest_sampler.get());
        cmd.draw_elements    (gl::Draw_elements_type::unsigned_short,
                              text_pass_two_draw_offset);
    }

    surface->swap_buffers();
}

}
