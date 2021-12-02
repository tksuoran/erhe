#pragma once

#include "erhe/graphics/buffer.hpp"
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
#include <string_view>
#include <vector>

namespace erhe::graphics
{
    class Buffer;
    class OpenGL_state_tracker;
    class Sampler;
    class Shader_stages;
}

namespace erhe::scene
{
    class Camera;
    class Viewport;
}

namespace erhe::ui
{
    class Font;
}

namespace editor
{

class Text_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Text_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Text_renderer ();
    ~Text_renderer() override;
    Text_renderer (const Text_renderer&) = delete;
    void operator=(const Text_renderer&) = delete;
    Text_renderer (Text_renderer&&)      = delete;
    void operator=(Text_renderer&&)      = delete;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    void print(
        const glm::vec3        text_position,
        const uint32_t         text_color,
        const std::string_view text
    );

    void render(erhe::scene::Viewport viewport);
    void next_frame();

private:
    static constexpr size_t s_frame_resources_count = 4;

    class Frame_resources
    {
    public:
        static constexpr gl::Buffer_storage_mask storage_mask{
            gl::Buffer_storage_mask::map_coherent_bit   |
            gl::Buffer_storage_mask::map_persistent_bit |
            gl::Buffer_storage_mask::map_write_bit
        };

        static constexpr gl::Map_buffer_access_mask access_mask{
            gl::Map_buffer_access_mask::map_coherent_bit   |
            gl::Map_buffer_access_mask::map_persistent_bit |
            gl::Map_buffer_access_mask::map_write_bit
        };

        Frame_resources(
            size_t                                    vertex_count,
            erhe::graphics::Shader_stages*            shader_stages,
            erhe::graphics::Vertex_attribute_mappings attribute_mappings,
            erhe::graphics::Vertex_format&            vertex_format,
            erhe::graphics::Buffer*                   index_buffer
        )
            : vertex_buffer{
                gl::Buffer_target::array_buffer,
                vertex_format.stride() * vertex_count,
                storage_mask,
                access_mask
            }
            , projection_buffer{
                gl::Buffer_target::uniform_buffer,
                256,
                storage_mask,
                access_mask
            }
            , vertex_input_state{
                attribute_mappings,
                vertex_format,
                &vertex_buffer,
                index_buffer
            }
            , pipeline{
                shader_stages,
                &vertex_input_state,
                &erhe::graphics::Input_assembly_state::triangle_fan,
                &erhe::graphics::Rasterization_state::cull_mode_none,
                //&erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled,
                &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                &erhe::graphics::Color_blend_state::color_blend_premultiplied,
                nullptr
            }
        {
            vertex_buffer.set_debug_label("Text Renderer Vertex");
            vertex_buffer.set_debug_label("Projection Vertex");
        }

        Frame_resources(const Frame_resources&) = delete;
        auto operator= (const Frame_resources&) -> Frame_resources& = delete;
        Frame_resources(Frame_resources&&) = delete;
        auto operator= (Frame_resources&&) = delete;

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             projection_buffer;
        erhe::graphics::Vertex_input_state vertex_input_state;
        erhe::graphics::Pipeline           pipeline;
    };

    void create_frame_resources();

    auto current_frame_resources() -> Frame_resources&;

    erhe::graphics::OpenGL_state_tracker*            m_pipeline_state_tracker{nullptr};

    erhe::graphics::Fragment_outputs                 m_fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings        m_attribute_mappings;
    erhe::graphics::Vertex_format                    m_vertex_format;
    std::shared_ptr<erhe::graphics::Buffer>          m_index_buffer;
    std::unique_ptr<erhe::graphics::Shader_resource> m_projection_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   m_shader_stages;
    std::unique_ptr<erhe::ui::Font>                  m_font;
    erhe::graphics::Shader_resource                  m_default_uniform_block; // containing sampler uniforms
    std::unique_ptr<erhe::graphics::Sampler>         m_nearest_sampler;
    int                                              m_font_sampler_location{0};

    std::deque<Frame_resources> m_frame_resources;
    size_t                      m_current_frame_resource_slot{0};

    class Buffer_range
    {
    public:
        size_t first_byte_offset{0};
        size_t byte_count       {0};
    };

    class Buffer_writer
    {
    public:
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
            range.byte_count        = 0;
            write_offset            = 0;
        }
    };

    Buffer_writer m_vertex_writer;
    size_t        m_quad_count{0};
};

}
