#pragma once

#include "erhe/application/renderers/buffer_writer.hpp"
//#include "hextiles/globals.hpp"

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
#include <glm/glm.hpp>

#include <bitset>
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
    class Texture;
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

namespace hextiles
{

class Map_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Map_renderer"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Map_renderer  ();
    ~Map_renderer () noexcept override;
    Map_renderer  (const Map_renderer&) = delete;
    void operator=(const Map_renderer&) = delete;
    Map_renderer  (Map_renderer&&)      = delete;
    void operator=(Map_renderer&&)      = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    [[nodiscard]] auto tileset_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&;

    void blit(
        int      src_x,
        int      src_y,
        int      src_width,
        int      src_height,
        float    dst_x0,
        float    dst_y0,
        float    dst_x1,
        float    dst_y1,
        uint32_t color
    );

    void begin    ();
    void end      ();

    void render    (erhe::scene::Viewport viewport);
    void next_frame();

private:
    gsl::span<float>    m_gpu_float_data;
    gsl::span<uint32_t> m_gpu_uint_data;
    size_t              m_word_offset;
    bool                m_can_blit{false};

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
            const size_t                              vertex_count,
            erhe::graphics::Shader_stages*            shader_stages,
            erhe::graphics::Vertex_attribute_mappings attribute_mappings,
            erhe::graphics::Vertex_format&            vertex_format,
            erhe::graphics::Buffer*                   index_buffer,
            const size_t                              slot
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
    void create_frame_resources(size_t vertex_count);

    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    erhe::graphics::Fragment_outputs                 m_fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings        m_attribute_mappings;
    erhe::graphics::Vertex_format                    m_vertex_format;
    std::shared_ptr<erhe::graphics::Buffer>          m_index_buffer;
    std::unique_ptr<erhe::graphics::Shader_resource> m_projection_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   m_shader_stages;
    size_t                                           m_u_clip_from_window_size  {0};
    size_t                                           m_u_clip_from_window_offset{0};
    size_t                                           m_u_texture_size           {0};
    size_t                                           m_u_texture_offset         {0};
    std::unique_ptr<erhe::graphics::Sampler>         m_nearest_sampler;
    std::shared_ptr<erhe::graphics::Texture>         m_tileset_texture;

    std::deque<Frame_resources>      m_frame_resources;
    size_t                           m_current_frame_resource_slot{0};

    erhe::application::Buffer_writer m_vertex_writer;
    erhe::application::Buffer_writer m_projection_writer;
    size_t                           m_index_range_first{0};
    size_t                           m_index_count      {0};
};

} // namespace hextiles
