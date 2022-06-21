#pragma once

#include "erhe/application/renderers/buffer_writer.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/ui/rectangle.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics
{
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

namespace erhe::application
{

class Text_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Text_renderer"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Text_renderer ();
    ~Text_renderer() noexcept override;
    Text_renderer (const Text_renderer&) = delete;
    void operator=(const Text_renderer&) = delete;
    Text_renderer (Text_renderer&&)      = delete;
    void operator=(Text_renderer&&)      = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    void print(
        const glm::vec3        text_position,
        const uint32_t         text_color,
        const std::string_view text
    );
    auto measure(const std::string_view text) const -> erhe::ui::Rectangle;

    void render    (erhe::scene::Viewport viewport);
    void next_frame();

private:
    static constexpr std::size_t s_frame_resources_count = 4;

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
            const std::size_t                         vertex_count,
            erhe::graphics::Shader_stages*            shader_stages,
            erhe::graphics::Vertex_attribute_mappings attribute_mappings,
            erhe::graphics::Vertex_format&            vertex_format,
            erhe::graphics::Buffer*                   index_buffer,
            const std::size_t                         slot
        );

        Frame_resources(const Frame_resources&) = delete;
        auto operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&) = delete;
        auto operator= (Frame_resources&&) = delete;

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             projection_buffer;
        erhe::graphics::Vertex_input_state vertex_input;
        erhe::graphics::Pipeline           pipeline;
    };

    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;
    void create_frame_resources();

    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    erhe::graphics::Fragment_outputs                 m_fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings        m_attribute_mappings;
    erhe::graphics::Vertex_format                    m_vertex_format;
    std::shared_ptr<erhe::graphics::Buffer>          m_index_buffer;
    std::unique_ptr<erhe::graphics::Shader_resource> m_projection_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   m_shader_stages;
    erhe::graphics::Shader_resource                  m_default_uniform_block; // containing sampler uniforms for non bindless textures

    std::size_t                                      m_u_clip_from_window_size  {0};
    std::size_t                                      m_u_clip_from_window_offset{0};
    std::size_t                                      m_u_texture_size           {0};
    std::size_t                                      m_u_texture_offset         {0};

    std::unique_ptr<erhe::ui::Font>                  m_font;
    std::unique_ptr<erhe::graphics::Sampler>         m_nearest_sampler;

    std::deque<Frame_resources> m_frame_resources;
    std::size_t                 m_current_frame_resource_slot{0};

    Buffer_writer m_vertex_writer;
    Buffer_writer m_projection_writer;
    std::size_t   m_index_range_first{0};
    std::size_t   m_index_count      {0};
};

} // namespace erhe::application
