#pragma once

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/components/component.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace erhe::graphics
{
    class Buffer;
    class OpenGL_state_tracker;
    class Sampler;
    class Shader_stages;
    class Shader_monitor;
}

namespace erhe::scene
{
    class ICamera;
    struct Viewport;
}

namespace erhe::ui
{
    class Font;
}

namespace sample
{

struct Line
{
    glm::vec3 p0;
    glm::vec3 p1;
};

class Line_renderer
    : public erhe::components::Component
{
public:
    Line_renderer();

    virtual ~Line_renderer();

    void connect(std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker,
                 std::shared_ptr<erhe::graphics::Shader_monitor>       shader_monitor);

    void initialize_component() override;

    void render(erhe::scene::Viewport       camera_viewport,
                const erhe::scene::ICamera& camera);

    void set_line_color(uint32_t color)
    {
        m_line_color = color;
    }

    void add_lines(const std::initializer_list<Line> lines, float thickness = 2.0f);

    void next_frame();

private:
    static constexpr size_t s_frame_resources_count = 4;

    struct Frame_resources
    {
        static inline const erhe::graphics::Color_blend_state color_blend_visible_lines{
            true, // enabled
            { // rgb
                gl::Blend_equation_mode::func_add,
                gl::Blending_factor::src_alpha,
                gl::Blending_factor::one_minus_src_alpha
            },
            { // alpha
                gl::Blend_equation_mode::func_add,
                gl::Blending_factor::src_alpha,
                gl::Blending_factor::one_minus_src_alpha
            },
            glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}, // constant
            true,
            true,
            true,
            true
        };
        static inline const erhe::graphics::Color_blend_state color_blend_hidden_lines{
            true, // enabled
            { // rgb
                gl::Blend_equation_mode::func_add,
                gl::Blending_factor::src_alpha,
                gl::Blending_factor::one_minus_src_alpha
            },
            { // alpha
                gl::Blend_equation_mode::func_add,
                gl::Blending_factor::src_alpha,
                gl::Blending_factor::one_minus_src_alpha
            },
            glm::vec4{0.0f, 0.0f, 0.0f, 0.3f}, // constant
            true,
            true,
            true,
            true
        };
        static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_coherent_bit   |
                                                              gl::Buffer_storage_mask::map_persistent_bit |
                                                              gl::Buffer_storage_mask::map_write_bit};

        static constexpr gl::Map_buffer_access_mask access_mask{gl::Map_buffer_access_mask::map_coherent_bit   |
                                                                gl::Map_buffer_access_mask::map_persistent_bit |
                                                                gl::Map_buffer_access_mask::map_write_bit};

        Frame_resources(size_t                                    vertex_count,
                        erhe::graphics::Shader_stages*            shader_stages,
                        erhe::graphics::Vertex_attribute_mappings attribute_mappings,
                        erhe::graphics::Vertex_format&            vertex_format)

            : vertex_buffer     {gl::Buffer_target::array_buffer,
                                 vertex_format.stride() * vertex_count,
                                 storage_mask,
                                 access_mask}

            , model_buffer      {gl::Buffer_target::uniform_buffer,
                                 256,
                                 storage_mask,
                                 access_mask}

            , vertex_input_state{attribute_mappings,
                                 vertex_format,
                                 &vertex_buffer,
                                 nullptr}

            , pipeline_depth_pass{shader_stages,
                                 &vertex_input_state,
                                 &erhe::graphics::Input_assembly_state::lines,
                                 &erhe::graphics::Rasterization_state::cull_mode_none,
                                 &erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled,
                                 &erhe::graphics::Color_blend_state::color_blend_premultiplied,
                                 nullptr}
            , pipeline_depth_fail{shader_stages,
                                 &vertex_input_state,
                                 &erhe::graphics::Input_assembly_state::lines,
                                 &erhe::graphics::Rasterization_state::cull_mode_none,
                                 &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                                 &color_blend_hidden_lines,
                                 nullptr}
        {
            vertex_buffer.set_debug_label("Line Renderer Vertex");
            model_buffer.set_debug_label ("Line Renderer Model");
        }

        Frame_resources(const Frame_resources& other) = delete;
        auto operator=(const Frame_resources&) -> Frame_resources& = delete;

        Frame_resources(Frame_resources&& other) noexcept
        {
            vertex_buffer                    = std::move(other.vertex_buffer);
            model_buffer                     = std::move(other.model_buffer);
            vertex_input_state               = std::move(other.vertex_input_state);
            pipeline_depth_pass              = std::move(other.pipeline_depth_pass);
            pipeline_depth_fail              = std::move(other.pipeline_depth_fail);
            pipeline_depth_pass.vertex_input = &vertex_input_state;
            pipeline_depth_fail.vertex_input = &vertex_input_state;
        }

        auto operator=(Frame_resources&& other) noexcept -> Frame_resources&
        {
            vertex_buffer                    = std::move(other.vertex_buffer);
            model_buffer                     = std::move(other.model_buffer);
            vertex_input_state               = std::move(other.vertex_input_state);
            pipeline_depth_pass              = std::move(other.pipeline_depth_pass);
            pipeline_depth_fail              = std::move(other.pipeline_depth_fail);
            pipeline_depth_pass.vertex_input = &vertex_input_state;
            pipeline_depth_fail.vertex_input = &vertex_input_state;
            return *this;
        }

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             model_buffer;
        erhe::graphics::Vertex_input_state vertex_input_state;
        erhe::graphics::Pipeline           pipeline_depth_pass;
        erhe::graphics::Pipeline           pipeline_depth_fail;
    };

    void create_frame_resources();

    auto current_frame_resources() -> Frame_resources&;


    uint32_t m_line_color{0xffffffffu};

    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::graphics::Shader_monitor>       m_shader_monitor;
    erhe::graphics::Fragment_outputs                      m_fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings             m_attribute_mappings;
    erhe::graphics::Vertex_format                         m_vertex_format;
    erhe::graphics::Shader_resource                       m_model_block{"model", 0, erhe::graphics::Shader_resource::Type::uniform_block};
    std::unique_ptr<erhe::graphics::Shader_stages>        m_shader_stages;
    erhe::graphics::Shader_resource                       m_default_uniform_block; // containing sampler uniforms

    std::vector<Frame_resources> m_frame_resources;
    size_t                       m_current_frame_resource_slot{0};

    struct Buffer_range
    {
        size_t first_byte_offset{0};
        size_t byte_count       {0};
    };

    struct Buffer_writer
    {
        Buffer_range range;
        size_t       write_offset{0};

        void begin()
        {
            range.first_byte_offset = write_offset;
        }

        void end()
        {
            range.byte_count = write_offset - range.first_byte_offset;
        }

        void reset()
        {
            range.first_byte_offset = 0;
            range.byte_count = 0;
            write_offset = 0;
        }
    };

    Buffer_writer m_vertex_writer;
    size_t        m_line_count{0};

    size_t        m_clip_from_world_offset       {0};
    size_t        m_view_position_in_world_offset{0};
};

}
