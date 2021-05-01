#ifndef gui_renderer_hpp_erhe_ui
#define gui_renderer_hpp_erhe_ui

#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/multi_buffer.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"

#include "erhe/graphics_experimental/image_transfer.hpp"
#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/graphics_experimental/shader_monitor.hpp"

#include "erhe/components/component.hpp"
#include <filesystem>
#include <glm/glm.hpp>
#include <map>
#include <memory>

namespace erhe::graphics
{

class Buffer;
class Render_group;
class Shader_stages;
class Shader_monitor;
class Storage_buffer_range;
class Vertex_format;
class Vertex_input_state;

} // namespace erhe::graphics

namespace erhe::ui
{

class Font;
class Ninepatch_style;
class Style;

struct Gui_uniforms
{
    Gui_uniforms() = default;

    size_t clip_from_model{0}; // mat4
    size_t color          {0}; // vec4
    size_t color_add      {0}; // vec4
    size_t color_scale    {0}; // vec4
    size_t hsv_matrix     {0}; // mat4
    size_t t              {0}; // float
};

struct Gui_uniform_state
{
    glm::mat4 clip_from_model{1.0f};
    glm::vec4 color          {1.0f};
    glm::vec4 color_add      {0.0f};
    glm::vec4 color_scale    {1.0f};
    glm::mat4 hsv_matrix     {1.0f};
    float     t              {0.0f};
};

class Gui_renderer
    : public erhe::components::Component
{

public:
    using index_t = uint16_t;

    Gui_renderer();

    Gui_renderer(const Gui_renderer&) = delete;

    auto operator=(const Gui_renderer&)
    -> Gui_renderer& = delete;

    virtual ~Gui_renderer() = default;

    void connect(std::shared_ptr<erhe::graphics::Render_group>& render_group);

    void initialize_component() override;

    auto load_program(const std::string& name, const std::string& path)
    -> std::unique_ptr<erhe::graphics::Shader_stages>;

    void prepare();

    void on_resize(int width, int height);

    auto ortho() const
    -> glm::mat4;

    auto map_indices(size_t index_offset, size_t index_count)
    -> gsl::span<index_t>;

    void unmap_indices();

    auto map_vertices(size_t vertex_offset, size_t vertex_count, size_t vertex_element_size = 0)
    -> gsl::span<float>;

    void unmap_vertices();

    auto default_style() const
    -> gsl::not_null<const Style*>;

    auto null_padding_style() const
    -> gsl::not_null<const Style*>;

    auto button_style() const
    -> gsl::not_null<const Style*>;

    auto menulist_style() const
    -> gsl::not_null<const Style*>;

    auto choice_style() const
    -> gsl::not_null<const Style*>;

    auto colorpicker_style() const
    -> gsl::not_null<const Style*>;

    auto slider_style() const
    -> gsl::not_null<const Style*>;

    auto render_group() const
    -> erhe::graphics::Render_group&;

    auto vertex_format() const
    -> const erhe::graphics::Vertex_format&;

    auto vertex_stream() const
     -> const erhe::graphics::Vertex_input_state&;

    auto vertex_buffer()
    -> std::shared_ptr<erhe::graphics::Buffer>&;

    auto index_buffer()
    -> std::shared_ptr<erhe::graphics::Buffer>&;

    auto image_transfer()
    -> erhe::graphics::ImageTransfer&
    {
        return m_image_transfer;
    };

    void record_uniforms(erhe::graphics::Render_group::draw_index_t draw_index);

    auto current_uniform_state()
    -> Gui_uniform_state &
    {
        return m_current_uniform_state;
    }

    auto get_uniform_block()
    -> erhe::graphics::Shader_resource&
    {
        return m_uniform_block;
    }

private:
    erhe::graphics::ImageTransfer                   m_image_transfer;
    std::shared_ptr<erhe::graphics::Render_group>   m_render_group;
    erhe::graphics::Shader_resource         m_uniform_block{"gui", 0, erhe::graphics::Shader_resource::Type::uniform_block};
    Gui_uniform_state                               m_current_uniform_state;
    Gui_uniforms                                    m_uniforms;     /// offsets in block for each uniform

    std::shared_ptr<erhe::graphics::Shader_monitor> m_shader_monitor;
    std::filesystem::path                           m_shader_path;
    std::vector<std::pair<std::string, int>>        m_shader_versions;

    erhe::graphics::Shader_resource                 m_default_uniform_block; // containing sampler uniforms

    erhe::graphics::Pipeline                        m_gui_render_states;
    erhe::graphics::Sampler                         m_nearest_sampler_uniform;
    erhe::graphics::Vertex_attribute_mappings       m_vertex_attribute_mappings;
    erhe::graphics::Fragment_outputs                m_fragment_outputs;
    erhe::graphics::Vertex_format                   m_vertex_format;

    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input_state;
    std::shared_ptr<erhe::graphics::Buffer>             m_vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer>             m_index_buffer;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_ninepatch_program;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_font_program;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_hsv_program;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_slider_program;

    std::unique_ptr<erhe::graphics::Sampler>        m_nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>        m_linear_sampler;

    glm::mat4                                       m_ortho{1.0f};
    std::unique_ptr<Font>                           m_font;

    std::unique_ptr<Style>                          m_default_style;
    std::unique_ptr<Style>                          m_null_padding_style;
    std::unique_ptr<Ninepatch_style>                m_button_ninepatch_style;
    std::unique_ptr<Ninepatch_style>                m_menulist_ninepatch_style;
    std::unique_ptr<Ninepatch_style>                m_slider_ninepatch_style;
    std::unique_ptr<Style>                          m_button_style;
    std::unique_ptr<Style>                          m_menulist_style;
    std::unique_ptr<Style>                          m_choice_style;
    std::unique_ptr<Style>                          m_colorpicker_style;
    std::unique_ptr<Style>                          m_slider_style;

    gsl::span<index_t>                              m_map_index_data;
    gsl::span<float>                                m_map_vertex_data;
};

} // namespace erhe::ui

#endif
