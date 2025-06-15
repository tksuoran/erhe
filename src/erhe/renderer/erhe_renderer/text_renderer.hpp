#pragma once

#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
//#include "erhe_ui/font.hpp"
#include "erhe_ui/rectangle.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace erhe::graphics {
    class Gl_context_provider;
    class Device;
    class OpenGL_state_tracker;
    class Sampler;
    class Shader_monitor;
    class Shader_stages;
}
namespace erhe::ui {
    class Font;
}

namespace erhe::renderer {

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

    explicit Text_renderer(erhe::graphics::Device& graphics_device);
    ~Text_renderer();

    Text_renderer (const Text_renderer&) = delete;
    void operator=(const Text_renderer&) = delete;
    Text_renderer (Text_renderer&&)      = delete;
    void operator=(Text_renderer&&)      = delete;

    // Public API
    void print(const glm::vec3 text_position, uint32_t text_color, const std::string_view text);
    [[nodiscard]] auto font_size() -> float;
    [[nodiscard]] auto measure  (const std::string_view text) const -> erhe::ui::Rectangle;

    void render(erhe::math::Viewport viewport);

private:
    auto build_shader_stages() -> erhe::graphics::Shader_stages_prototype;

    static constexpr std::size_t s_vertex_count{65536 * 8};

    static constexpr std::size_t uint16_max              {65535};
    static constexpr std::size_t uint16_primitive_restart{0xffffu};
    static constexpr std::size_t per_quad_vertex_count   {4}; // corner count
    static constexpr std::size_t per_quad_index_count    {per_quad_vertex_count + 1}; // Plus one for primitive restart
    static constexpr std::size_t max_quad_count          {uint16_max / per_quad_vertex_count}; // each quad consumes 4 indices
    static constexpr std::size_t index_count             {uint16_max * per_quad_index_count};
    static constexpr std::size_t index_stride            {2};

    erhe::graphics::Device&                m_graphics_device;
    erhe::graphics::Shader_resource        m_default_uniform_block; // containing sampler uniforms for non bindless textures
    erhe::graphics::Shader_resource        m_projection_block;
    erhe::graphics::Shader_resource        m_vertex_ssbo_block;
    erhe::graphics::Shader_resource*       m_clip_from_window_resource;
    erhe::graphics::Shader_resource*       m_texture_resource;
    erhe::graphics::Shader_resource*       m_vertex_data_resource;
    std::size_t                            m_u_clip_from_window_size  {0};
    std::size_t                            m_u_clip_from_window_offset{0};
    std::size_t                            m_u_texture_size           {0};
    std::size_t                            m_u_texture_offset         {0};
    std::size_t                            m_u_vertex_data_size       {0};
    std::size_t                            m_u_vertex_data_offset     {0};
    erhe::graphics::Fragment_outputs       m_fragment_outputs;
    erhe::graphics::Sampler                m_nearest_sampler;
    erhe::graphics::Shader_stages          m_shader_stages;
    std::unique_ptr<erhe::ui::Font>        m_font;
    erhe::graphics::GPU_ring_buffer_client m_vertex_ssbo_buffer;
    erhe::graphics::GPU_ring_buffer_client m_projection_buffer;
    erhe::graphics::Vertex_input_state     m_vertex_input;
    erhe::graphics::Render_pipeline_state  m_pipeline;

    std::vector<erhe::graphics::Buffer_range> m_vertex_buffer_ranges;
};

} // namespace erhe::renderer
