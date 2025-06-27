#pragma once

#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include <vector>

namespace erhe::graphics {
    class Shader_stages;
}

namespace erhe::renderer {

class Debug_renderer;

class Debug_renderer_config
{
public:
    gl::Primitive_type primitive_type   {gl::Primitive_type{0}};
    unsigned int       stencil_reference{0};
    bool               draw_visible     {true};
    bool               draw_hidden      {false};
};

auto operator==(const Debug_renderer_config& lhs, const Debug_renderer_config& rhs) -> bool;

class Debug_draw_entry
{
public:
    erhe::graphics::Buffer_range input_buffer_range;
    erhe::graphics::Buffer_range draw_buffer_range;
    std::size_t                  primitive_count;
};

class Debug_renderer_bucket
{
public:
    Debug_renderer_bucket(erhe::graphics::Device& graphics_device, Debug_renderer& debug_renderer, Debug_renderer_config config);

    void clear           ();
    auto match           (const Debug_renderer_config& config) const -> bool;
    void dispatch_compute();
    void render          (bool draw_hidden, bool draw_visible);
    void release_buffers ();
    auto make_draw       (std::size_t vertex_byte_count, std::size_t primitive_count) -> std::span<std::byte>;

private:
    [[nodiscard]] auto make_pipeline(bool visible, const bool reverse_depth = true) -> erhe::graphics::Render_pipeline_state;

    erhe::graphics::Device&                m_graphics_device;
    Debug_renderer&                        m_debug_renderer;
    erhe::graphics::GPU_ring_buffer_client m_vertex_ssbo_buffer;
    erhe::graphics::GPU_ring_buffer_client m_triangle_vertex_buffer;
    Debug_renderer_config                  m_config;
    erhe::graphics::Shader_stages*         m_compute_shader_stages{nullptr};
    erhe::graphics::Render_pipeline_state  m_compute;
    erhe::graphics::Render_pipeline_state  m_pipeline_visible;
    erhe::graphics::Render_pipeline_state  m_pipeline_hidden;
    std::vector<Debug_draw_entry>          m_draws;
};

} // namespace erhe::renderer

