#pragma once

#include "erhe/application/renderers/buffer_writer.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/viewport.hpp"

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Sampler;
    class Shader_resource;
    class Texture;
    class Vertex_input_state;
}

namespace erhe::application
{

class Configuration;
class Mesh_memory;

class Quad_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Quad_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Quad_renderer ();
    ~Quad_renderer() noexcept override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    void render(const erhe::graphics::Texture& texture);

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
            erhe::graphics::Shader_stages* shader_stages,
            const size_t                   slot
        );

        Frame_resources(const Frame_resources&) = delete;
        auto operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&) = delete;
        auto operator= (Frame_resources&&) = delete;

        erhe::graphics::Buffer             parameter_buffer;
        erhe::graphics::Vertex_input_state vertex_input;
        erhe::graphics::Pipeline           pipeline;
    };

    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;
    void create_frame_resources();

    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    erhe::graphics::Fragment_outputs                    m_fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings           m_attribute_mappings;
    erhe::graphics::Vertex_format                       m_vertex_format;
    std::unique_ptr<erhe::graphics::Shader_resource>    m_parameter_block;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_shader_stages;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
    size_t                                              m_u_texture_size  {0};
    size_t                                              m_u_texture_offset{0};
    std::unique_ptr<erhe::graphics::Sampler>            m_nearest_sampler;

    std::deque<Frame_resources> m_frame_resources;
    size_t                      m_current_frame_resource_slot{0};

    Buffer_writer m_parameter_writer;
};

} // namespace erhe::application
