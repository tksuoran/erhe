#ifndef menu_hpp
#define menu_hpp

#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/components/component.hpp"

namespace erhe::graphics
{
class Command_buffer;
class ImageTransfer;
class Multi_buffer;
class OpenGL_state_tracker;
class Sampler;
class Uniform_buffer;
class Storage_buffer_range;
} // namespace erhe::graphics

namespace erhe::ui
{
class Font;
class Style;
class Text_buffer;
} // namespace erhe::ui

namespace editor {

class Application;
class Programs;
class Textures;

class Menu
    : public erhe::toolkit::View
    , public erhe::components::Component
{
public:
    Menu();

    virtual ~Menu() = default;

    void connect(std::shared_ptr<Application>                          application,
                 std::shared_ptr<erhe::graphics::Command_buffer>       command_buffer,
                 std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker,
                 std::shared_ptr<Programs>                             programs,
                 std::shared_ptr<Textures>                             textures);

    void disconnect();

    void initialize_component() override;

    void on_load();

    void update() override;

    void on_enter() override;

    void on_resize(int width, int height) override;

private:
    void resize(float w, float h);

    void render();

    void initialize_background();

    class Frame_resources
    {
    public:
        static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_coherent_bit   |
                                                              gl::Buffer_storage_mask::map_persistent_bit |
                                                              gl::Buffer_storage_mask::map_write_bit};

        static constexpr gl::Map_buffer_access_mask access_mask{gl::Map_buffer_access_mask::map_coherent_bit   |
                                                                gl::Map_buffer_access_mask::map_persistent_bit |
                                                                gl::Map_buffer_access_mask::map_write_bit};

        explicit Frame_resources(Imgui_renderer& imgui_renderer)
            : draw_parameter_buffer{gl::Buffer_target::shader_storage_buffer,
                                    Imgui_renderer::max_draw_count,
                                    imgui_renderer.draw_parameter_block->size_bytes(),
                                    storage_mask,
                                    access_mask}
            , index_buffer{gl::Buffer_target::element_array_buffer,
                           Imgui_renderer::max_index_count,
                           sizeof(uint16_t),
                           storage_mask,
                           access_mask}
            , vertex_buffer{gl::Buffer_target::array_buffer,
                            Imgui_renderer::max_vertex_count,
                            imgui_renderer.vertex_format.stride(),
                            storage_mask,
                            access_mask}
            , draw_indirect_buffer{gl::Buffer_target::draw_indirect_buffer,
                                   Imgui_renderer::max_draw_count,
                                   sizeof(gl::Draw_elements_indirect_command),
                                   storage_mask,
                                   access_mask}
            , vertex_input_state{imgui_renderer.attribute_mappings,
                                 imgui_renderer.vertex_format,
                                 &vertex_buffer,
                                 &index_buffer}
            , pipeline{imgui_renderer.shader_stages.get(),
                       &vertex_input_state,
                       &erhe::graphics::Input_assembly_state::triangles,
                       &erhe::graphics::Rasterization_state::cull_mode_none,
                       &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled, 
                       &imgui_renderer.color_blend_state,
                       nullptr}
        {
        }

        Frame_resources(const Frame_resources& other) = delete;

        auto operator=(const Frame_resources&)
        -> Frame_resources& = delete;

        Frame_resources(Frame_resources&& other) noexcept
        {
            std::swap(vertex_buffer,         other.vertex_buffer);
            std::swap(index_buffer,          other.index_buffer);
            std::swap(draw_parameter_buffer, other.draw_parameter_buffer);
            std::swap(draw_indirect_buffer,  other.draw_indirect_buffer);
            std::swap(vertex_input_state,    other.vertex_input_state);
            std::swap(pipeline,              other.pipeline);
            pipeline.vertex_input = &vertex_input_state;
        }

        auto operator=(Frame_resources&& other) noexcept
        -> Frame_resources&
        {
            std::swap(vertex_buffer,         other.vertex_buffer);
            std::swap(index_buffer,          other.index_buffer);
            std::swap(draw_parameter_buffer, other.draw_parameter_buffer);
            std::swap(draw_indirect_buffer,  other.draw_indirect_buffer);
            std::swap(vertex_input_state,    other.vertex_input_state);
            std::swap(pipeline,              other.pipeline);
            pipeline.vertex_input = &vertex_input_state;
            return *this;
        }

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             index_buffer;
        erhe::graphics::Buffer             draw_parameter_buffer;
        erhe::graphics::Buffer             draw_indirect_buffer;
        erhe::graphics::Vertex_input_state vertex_input_state;
        erhe::graphics::Pipeline           pipeline;
    };

    std::shared_ptr<erhe::graphics::Command_buffer>       m_command_buffer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Textures>                             m_textures;
    std::shared_ptr<Application>                          m_application;

    erhe::graphics::Vertex_attribute_mappings           m_vertex_attribute_mappings;
    erhe::graphics::Vertex_format                       m_vertex_format;

    std::unique_ptr<erhe::primitive::Mesh>                     m_background_mesh;

    gl::Draw_arrays_indirect_command               m_background_draw;

    gsl::span<std::byte>                           m_uniform_data;

    erhe::graphics::Viewport_state                 m_viewport;

    erhe::graphics::Color_blend_state              m_font_first_pass_color_blend;
    erhe::graphics::Color_blend_state              m_font_second_pass_color_blend;

    erhe::graphics::Pipeline                       m_background_pipeline;
    erhe::graphics::Pipeline                       m_font_first_pass_pipeline;
    erhe::graphics::Pipeline                       m_font_second_pass_pipeline;

    std::unique_ptr<erhe::ui::Font>                m_font;
    std::unique_ptr<erhe::ui::Style>               m_text_style;
    std::unique_ptr<erhe::ui::Text_buffer>         m_text_buffer;

    std::unique_ptr<erhe::graphics::Sampler>       m_nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>       m_linear_sampler;

    bool m_resize{false};
};

}

#endif
