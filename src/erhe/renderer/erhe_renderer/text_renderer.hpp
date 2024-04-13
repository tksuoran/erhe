#pragma once

#include "erhe_renderer/buffer_writer.hpp"

#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_ui/font.hpp"
#include "erhe_ui/rectangle.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <string_view>

namespace erhe::graphics {
    class Shader_monitor;
}
namespace erhe::ui {
    class Font;
}
namespace igl {
    class IBuffer;
    class IDevice;
    class IRenderPipelineState;
    class IShaderStages;
    class IVertexInputState;
}

namespace erhe::renderer
{

class Text_renderer
{
public:
    class Config
    {
    public:
        bool enabled{true};
        int  font_size{14};
    };
    Config config;

    explicit Text_renderer(erhe::graphics::Instance& graphics_instance);

    Text_renderer (const Text_renderer&) = delete;
    void operator=(const Text_renderer&) = delete;
    Text_renderer (Text_renderer&&)      = delete;
    void operator=(Text_renderer&&)      = delete;

    // Public API
    void print(const glm::vec3 text_position, uint32_t text_color, const std::string_view text);
    [[nodiscard]] auto font_size() -> float;
    [[nodiscard]] auto measure  (const std::string_view text) const -> erhe::ui::Rectangle;

    void render    (erhe::math::Viewport viewport);
    void next_frame();

private:
    static constexpr std::size_t s_frame_resources_count = 4;

    class Frame_resources
    {
    public:
        Frame_resources(
            igl::IDevice&                              device,
            bool                                       reverse_depth,
            std::size_t                                vertex_count,
            const std::shared_ptr<igl::IShaderStages>& shader_stages,
            erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
            erhe::graphics::Vertex_format&             vertex_format,
            const std::shared_ptr<igl::IBuffer>&       index_buffer,
            std::size_t                                slot
        );

        Frame_resources(const Frame_resources&) = delete;
        auto operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&) = delete;
        auto operator= (Frame_resources&&) = delete;

        std::shared_ptr<igl::IBuffer>              vertex_buffer;
        std::shared_ptr<igl::IBuffer>              projection_buffer;
        std::shared_ptr<igl::IVertexInputState>    vertex_input;
        std::shared_ptr<igl::IRenderPipelineState> pipeline;
    };

    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;
    void create_frame_resources();

    static constexpr std::size_t uint16_max              {65535};
    static constexpr std::size_t uint16_primitive_restart{0xffffu};
    static constexpr std::size_t per_quad_vertex_count   {4}; // corner count
    static constexpr std::size_t per_quad_index_count    {per_quad_vertex_count + 1}; // Plus one for primitive restart
    static constexpr std::size_t max_quad_count          {uint16_max / per_quad_vertex_count}; // each quad consumes 4 indices
    static constexpr std::size_t index_count             {uint16_max * per_quad_index_count};
    static constexpr std::size_t index_stride            {2};

    igl::IDevice&                             m_device;
    erhe::graphics::Shader_resource           m_default_uniform_block; // containing sampler uniforms for non bindless textures
    erhe::graphics::Shader_resource           m_projection_block;
    erhe::graphics::Shader_resource*          m_clip_from_window_resource;
    erhe::graphics::Shader_resource*          m_texture_resource;
    std::size_t                               m_u_clip_from_window_size  {0};
    std::size_t                               m_u_clip_from_window_offset{0};
    std::size_t                               m_u_texture_size           {0};
    std::size_t                               m_u_texture_offset         {0};
    erhe::graphics::Fragment_outputs          m_fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings m_attribute_mappings;
    erhe::graphics::Vertex_format             m_vertex_format;
    std::shared_ptr<igl::IBuffer>             m_index_buffer;
    std::shared_ptr<igl::ISamplerState>       m_nearest_sampler;
    Buffer_writer                             m_vertex_writer;
    Buffer_writer                             m_projection_writer;

    std::shared_ptr<igl::IShaderStages> m_shader_stages;
    std::unique_ptr<erhe::ui::Font>     m_font;
    std::deque<Frame_resources>         m_frame_resources;
    std::size_t                         m_current_frame_resource_slot{0};

    std::size_t m_index_range_first{0};
    std::size_t m_index_count      {0};
};

} // namespace erhe::renderer
