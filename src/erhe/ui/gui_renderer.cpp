#include "erhe/ui/gui_renderer.hpp"

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/span.hpp"
#include "erhe/graphics/shader_resource.hpp"

#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/graphics_experimental/shader_monitor.hpp"

#include "erhe/toolkit/file.hpp"
#include "erhe/ui/font.hpp"
#include "erhe/ui/log.hpp"
#include "erhe/ui/ninepatch.hpp"
#include "erhe/ui/ninepatch_style.hpp"
#include "erhe/ui/style.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cassert>
#include <filesystem>
#include <sstream>

namespace erhe::ui
{

using erhe::graphics::as_span;
using erhe::graphics::Buffer;
using erhe::graphics::Shader_stages;
using erhe::graphics::Sampler;
using erhe::graphics::Vertex_attribute;
using erhe::log::Log;
using glm::vec2;
using glm::vec4;
using glm::mat4;
using std::make_shared;
using std::make_unique;

Gui_renderer::Gui_renderer()
    : erhe::components::Component{"erhe::ui::Gui_renderer"}
{
}

void Gui_renderer::connect(std::shared_ptr<erhe::graphics::Render_group>& render_group)
{
    m_render_group = render_group;

    initialization_depends_on(render_group);
}


void Gui_renderer::initialize_component()
{
    log_gui_renderer.trace("Gui_renderer::initialize_component()");
    Log::Indenter indenter;

    m_uniforms.clip_from_model = m_uniform_block.add_mat4 ("clip_from_model").offset_in_parent();
    m_uniforms.color_add       = m_uniform_block.add_vec4 ("color_add"      ).offset_in_parent();
    m_uniforms.color_scale     = m_uniform_block.add_vec4 ("color_scale"    ).offset_in_parent();
    m_uniforms.hsv_matrix      = m_uniform_block.add_mat4 ("hsv_matrix"     ).offset_in_parent();
    m_uniforms.t               = m_uniform_block.add_float("t"              ).offset_in_parent();

    m_shader_path = std::filesystem::path("res/shaders/");

    m_shader_versions.emplace_back("5", 400);
    m_shader_versions.emplace_back("4", 150);
    m_shader_versions.emplace_back("0", 120);

    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec3, "a_position",      { Vertex_attribute::Usage_type::position,  0}, 0);
    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec3, "a_normal",        { Vertex_attribute::Usage_type::normal,    0}, 1);
    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec3, "a_normal_flat",   { Vertex_attribute::Usage_type::normal,    1}, 2);
    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec3, "a_normal_smooth", { Vertex_attribute::Usage_type::normal,    2}, 3);
    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec4, "a_color",         { Vertex_attribute::Usage_type::color,     0}, 4);
    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec2, "a_texcoord",      { Vertex_attribute::Usage_type::tex_coord, 1}, 5);
    m_vertex_attribute_mappings.add(gl::Attribute_type::float_vec4, "a_position_texcoord",
                                    {Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord, 0}, 0);

    m_vertex_format.make_attribute({Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord, 0 },
                                   gl::Attribute_type::float_vec4,
                                   {gl::Vertex_attrib_type::float_, false, 4});

    m_vertex_buffer = make_shared<Buffer>(gl::Buffer_target::array_buffer,
                                          128 * 1024,
                                          4 * sizeof(float),
                                          gl::Buffer_storage_mask::client_storage_bit |
                                          gl::Buffer_storage_mask::map_persistent_bit |
                                          gl::Buffer_storage_mask::map_write_bit);

    m_index_buffer = make_shared<Buffer>(gl::Buffer_target::element_array_buffer,
                                         512 * 1024,
                                         sizeof(uint16_t),
                                         gl::Buffer_storage_mask::client_storage_bit |
                                         gl::Buffer_storage_mask::map_persistent_bit |
                                         gl::Buffer_storage_mask::map_write_bit);

    m_vertex_input_state = make_unique<erhe::graphics::Vertex_input_state>(m_vertex_attribute_mappings,
                                                                           m_vertex_format,
                                                                           m_vertex_buffer.get(),
                                                                           m_index_buffer.get());

    m_fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    m_default_uniform_block.add_sampler("font_texture",       gl::Uniform_type::sampler_2d);
    m_default_uniform_block.add_sampler("background_texture", gl::Uniform_type::sampler_2d);

    m_ninepatch_program = load_program("ninepatch", "gui");
    m_slider_program    = load_program("slider",    "gui_slider");
    m_font_program      = load_program("Font",      "gui_font");
    m_hsv_program       = load_program("hsv",       "gui_hsv");

    m_font = make_unique<Font>("res/fonts/Ubuntu-R.ttf", 11, 1.0f);
    //m_font = make_unique<Font>("res/fonts/SourceCodePro-Semibold.ttf", 8, 1.0f);
    //m_font = make_unique<font>("res/fonts/SourceCodePro-Bold.ttf", 9, 1.0f);

    m_button_ninepatch_style   = make_unique<Ninepatch_style>(*this, "res/images/button_released.png", m_ninepatch_program.get(), 1);
    m_menulist_ninepatch_style = make_unique<Ninepatch_style>(*this, "res/images/shadow.png",          m_ninepatch_program.get(), 1);
    m_slider_ninepatch_style   = make_unique<Ninepatch_style>(*this, "res/images/button_released.png", m_slider_program.get(),    1);

    vec2 zero            { 0.0f,  0.0f};
    vec2 button_padding  {26.0f,  6.0f};
    vec2 menulist_padding{16.0f, 16.0f};
    vec2 inner_padding   { 6.0f,  6.0f};

    m_default_style      = make_unique<Style>(vec2(6.0f, 6.0f),
                                              vec2(6.0f, 6.0f));
    m_null_padding_style = make_unique<Style>(vec2(0.0f, 0.0f),
                                              vec2(0.0f, 0.0f));

    m_button_style       = make_unique<Style>(button_padding,
                                              inner_padding,
                                              m_font.get(),
                                              m_button_ninepatch_style.get(),
                                              m_font_program.get());

    m_slider_style       = make_unique<Style>(button_padding,
                                              inner_padding,
                                              m_font.get(),
                                              m_slider_ninepatch_style.get(),
                                              m_font_program.get());

    m_choice_style       = make_unique<Style>(zero,
                                              zero,
                                              m_font.get(),
                                              m_button_ninepatch_style.get(),
                                              m_font_program.get());

    m_menulist_style     = make_unique<Style>(menulist_padding,
                                              inner_padding,
                                              nullptr,
                                              m_menulist_ninepatch_style.get(),
                                              nullptr);

    m_colorpicker_style  = make_unique<Style>(menulist_padding,
                                              inner_padding,
                                              nullptr,
                                              m_menulist_ninepatch_style.get(),
                                              m_hsv_program.get());

    m_nearest_sampler = make_unique<Sampler>(gl::Texture_min_filter::nearest, gl::Texture_mag_filter::nearest);

    m_linear_sampler = make_unique<Sampler>(gl::Texture_min_filter::linear, gl::Texture_mag_filter::linear);

    Ensures(m_vertex_buffer);
    Ensures(m_index_buffer);
    Ensures(m_vertex_input_state);
    Ensures(m_ninepatch_program);
    Ensures(m_slider_program);
    Ensures(m_font_program);
    Ensures(m_hsv_program);
    Ensures(m_font);
    Ensures(m_button_ninepatch_style);
    Ensures(m_menulist_ninepatch_style);
    Ensures(m_slider_ninepatch_style);
    Ensures(m_default_style);
    Ensures(m_null_padding_style);
    Ensures(m_button_style);
    Ensures(m_slider_style);
    Ensures(m_choice_style);
    Ensures(m_menulist_style);
    Ensures(m_colorpicker_style);
    Ensures(m_nearest_sampler);
    Ensures(m_linear_sampler);
}

void Gui_renderer::prepare()
{
    Expects(m_vertex_input_state);

    //auto* vertex_stream = m_vertex_input_state.get();
    // TODO(tksuoran@gmail.com): Pipeline?
    //gl::bind_vertex_array(vertex_stream->gl_name());
}

void Gui_renderer::record_uniforms(erhe::graphics::Render_group::draw_index_t draw_index)
{
    log_render_group.trace("{}\n", __func__);
    Log::Indenter indenter;

    auto uniform_buffer = m_render_group->uniform_buffer();
    auto byte_offset = draw_index * m_uniform_block.size_bytes();
    uniform_buffer->write(byte_offset + m_uniforms.clip_from_model, as_span(m_current_uniform_state.clip_from_model));
    uniform_buffer->write(byte_offset + m_uniforms.color,           as_span(m_current_uniform_state.color));
    uniform_buffer->write(byte_offset + m_uniforms.color_add,       as_span(m_current_uniform_state.color_add));
    uniform_buffer->write(byte_offset + m_uniforms.color_scale,     as_span(m_current_uniform_state.color_scale));
    uniform_buffer->write(byte_offset + m_uniforms.hsv_matrix,      as_span(m_current_uniform_state.hsv_matrix));
    uniform_buffer->write(byte_offset + m_uniforms.t,               as_span(m_current_uniform_state.t));
}

void Gui_renderer::on_resize(int width, int height)
{
    m_ortho = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
}

auto Gui_renderer::ortho() const
-> glm::mat4
{
    return m_ortho;
}

auto Gui_renderer::default_style() const
-> gsl::not_null<const Style*>
{
    Expects(m_default_style);

    return m_default_style.get();
}

auto Gui_renderer::null_padding_style() const
-> gsl::not_null<const Style*>
{
    Expects(m_null_padding_style);

    return m_null_padding_style.get();
}

auto Gui_renderer::button_style() const
-> gsl::not_null<const Style*>
{
    Expects(m_button_style);

    return m_button_style.get();
}

auto Gui_renderer::menulist_style() const
-> gsl::not_null<const Style*>
{
    Expects(m_menulist_style);

    return m_menulist_style.get();
}

auto Gui_renderer::choice_style() const
-> gsl::not_null<const Style*>
{
    Expects(m_choice_style);

    return m_choice_style.get();
}

auto Gui_renderer::colorpicker_style() const
-> gsl::not_null<const Style*>
{
    Expects(m_colorpicker_style);

    return m_colorpicker_style.get();
}

auto Gui_renderer::slider_style() const
-> gsl::not_null<const Style*>
{
    Expects(m_slider_style);

    return m_slider_style.get();
}

auto Gui_renderer::render_group() const
-> erhe::graphics::Render_group&
{
    Expects(m_render_group);
    return *m_render_group;
}

auto Gui_renderer::vertex_format() const
-> const erhe::graphics::Vertex_format&
{
    return m_vertex_format;
}

auto Gui_renderer::vertex_stream() const
-> const erhe::graphics::Vertex_input_state&
{
    Expects(m_vertex_input_state);

    return *(m_vertex_input_state.get());
}

auto Gui_renderer::vertex_buffer()
-> std::shared_ptr<erhe::graphics::Buffer>&
{
    return m_vertex_buffer;
}

auto Gui_renderer::index_buffer()
-> std::shared_ptr<erhe::graphics::Buffer>&
{
    return m_index_buffer;
}

auto Gui_renderer::map_indices(size_t index_offset,
                               size_t index_count)
-> gsl::span<Gui_renderer::index_t>
{
    Expects(m_index_buffer);
    Expects(m_map_index_data.empty());

    auto raw_index_data = m_index_buffer->map_elements(index_offset,
                                                       index_count,
                                                       gl::Map_buffer_access_mask::map_write_bit |
                                                       gl::Map_buffer_access_mask::map_invalidate_range_bit);
    m_map_index_data = gsl::span(reinterpret_cast<index_t*>(raw_index_data.data()),
                                 raw_index_data.size_bytes() / sizeof(index_t));

    Ensures(!m_map_index_data.empty());

    return m_map_index_data;
}

void Gui_renderer::unmap_indices()
{
    Expects(m_index_buffer);
    Expects(!m_map_index_data.empty());

    m_index_buffer->unmap();
    m_map_index_data = gsl::span<Gui_renderer::index_t>();

    Ensures(m_map_index_data.empty());
}

auto Gui_renderer::map_vertices(size_t vertex_offset,
                                size_t vertex_count,
                                size_t vertex_element_size)
-> gsl::span<float>
{
    Expects(m_vertex_buffer);
    Expects(vertex_element_size == 0 || m_vertex_buffer->element_size() == vertex_element_size);
    Expects(m_map_vertex_data.empty());

    auto raw_vertex_data = m_vertex_buffer->map_elements(vertex_offset,
                                                         vertex_count,
                                                         gl::Map_buffer_access_mask::map_write_bit |
                                                         gl::Map_buffer_access_mask::map_invalidate_range_bit);
    m_map_vertex_data = gsl::span(reinterpret_cast<float*>(raw_vertex_data.data()),
                                  raw_vertex_data.size_bytes() / sizeof(float));

    Ensures(!m_map_vertex_data.empty());

    return m_map_vertex_data;
}

void Gui_renderer::unmap_vertices()
{
    Expects(m_vertex_buffer);
    Expects(m_map_vertex_data.empty());

    m_vertex_buffer->unmap();
    m_map_vertex_data = gsl::span<float>();

    Ensures(m_map_vertex_data.empty());
}

auto Gui_renderer::load_program(const std::string& name, const std::string& path)
-> std::unique_ptr<erhe::graphics::Shader_stages>
{
    for (auto& i : m_shader_versions)
    {
        auto vs_path = m_shader_path / std::filesystem::path(path + ".vs" + i.first + ".txt");
        auto fs_path = m_shader_path / std::filesystem::path(path + ".fs" + i.first + ".txt");

        if (!std::filesystem::exists(vs_path) || !std::filesystem::exists(fs_path))
        {
            continue;
        }

        log_gui_renderer.info("trying to use vs_path = {}, fs_path = {}\n", vs_path.string(), fs_path.string());

        Shader_stages::Create_info create_info(name, &m_default_uniform_block, &m_vertex_attribute_mappings, &m_fragment_outputs);
        create_info.add_interface_block(&m_uniform_block);
        create_info.shaders.emplace_back(gl::Shader_type::vertex_shader, vs_path);
        create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);

        Shader_stages::Prototype prototype(create_info);
        if (prototype.is_valid())
        {
            log_gui_renderer.info("   shader program OK\n");
            auto p = make_unique<Shader_stages>(std::move(prototype));
            if (m_shader_monitor != nullptr)
            {
                m_shader_monitor->add(vs_path, create_info, p.get());
                m_shader_monitor->add(fs_path, create_info, p.get());
            }
            return p;
        }
        log_gui_renderer.info("   shader program is not useful\n");
    }

    std::stringstream ss;
    ss << "Gui_renderer::load_program(" << name << ") failed";

    ss << "\nNo source files found.\n";
    ss << "Did you run export-files build target?\n";
    ss << "Current working directory is: " << std::filesystem::current_path() << "\n";
    for (auto& i : m_shader_versions)
    {
        auto vs_path = m_shader_path / std::filesystem::path(path + ".vs" + i.first + ".txt");
        auto fs_path = m_shader_path / std::filesystem::path(path + ".fs" + i.first + ".txt");

        ss << "\t" << vs_path << "\n";
        ss << "\t" << fs_path << "\n";
    }

    log_gui_renderer.error("{}\n", ss.str());
    return {};
}

} // namespace erhe::ui
