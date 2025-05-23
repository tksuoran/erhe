#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_renderer/gpu_ring_buffer.hpp"
#include "erhe_renderer/line_renderer_bucket.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_math/viewport.hpp"

#include <etl/vector.h>

#include <cstdint>
#include <memory>

namespace erhe::graphics {
    class Buffer;
    class Shader_stages;
}
namespace erhe::scene {
    class Camera;
    class Transform;
}

namespace erhe::renderer {

class Line_renderer_program_interface
{
public:
    explicit Line_renderer_program_interface(erhe::graphics::Instance& graphics_instance);

    bool                                             reverse_depth{false};
    erhe::graphics::Fragment_outputs                 fragment_outputs;
    erhe::dataformat::Vertex_format                  line_vertex_format;
    erhe::dataformat::Vertex_format                  triangle_vertex_format;
    std::unique_ptr<erhe::graphics::Shader_resource> line_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> line_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource> view_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   compute_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>   graphics_shader_stages;
    std::size_t                                      clip_from_world_offset       {0};
    std::size_t                                      view_position_in_world_offset{0};
    std::size_t                                      viewport_offset              {0};
    std::size_t                                      fov_offset                   {0};
};

class Scoped_line_renderer;
class Line_renderer_bucket;
class Line_renderer_config;

class Line_renderer
{
public:
    Line_renderer(erhe::graphics::Instance& graphics_instance);

    Line_renderer (const Line_renderer&) = delete; // TODO No longer needed? Was: Due to std::deque<Frame_resources> m_frame_resources
    void operator=(const Line_renderer&) = delete; // Style must be non-copyable and non-movable.
    Line_renderer (Line_renderer&&)      = delete;
    void operator=(Line_renderer&&)      = delete;

    // Public API
    void open        ();
    auto get         (const Line_renderer_config& config) -> Scoped_line_renderer;
    auto get         (unsigned int stencil, bool visible, bool hidden) -> Scoped_line_renderer;
    auto get_indirect(unsigned int stencil, bool visible, bool hidden) -> Scoped_line_renderer;

    void close ();
    void render    (const erhe::math::Viewport camera_viewport, const erhe::scene::Camera& camera);

    // API for Line_renderer_bucket
    auto get_line_offset           () const -> std::size_t;
    auto allocate_vertex_subspan   (std::size_t byte_count) -> std::span<std::byte>;
    auto get_program_interface     () const -> const Line_renderer_program_interface& { return m_program_interface; }
    auto get_line_vertex_buffer    () -> erhe::graphics::Buffer& { return m_line_vertex_buffer.get_buffer(); }
    auto get_triangle_vertex_buffer() -> erhe::graphics::Buffer& { return m_triangle_vertex_buffer.get_buffer(); }
    auto get_view_buffer           () -> erhe::graphics::Buffer& { return m_view_buffer.get_buffer(); }
    auto get_vertex_input          () -> erhe::graphics::Vertex_input_state* { return &m_vertex_input ;}
    auto verify_is_open            () const -> bool;

private:
    erhe::graphics::Instance&             m_graphics_instance;
    Line_renderer_program_interface       m_program_interface;

    GPU_ring_buffer                       m_line_vertex_buffer;
    GPU_ring_buffer                       m_triangle_vertex_buffer;
    GPU_ring_buffer                       m_view_buffer;
    erhe::graphics::Vertex_input_state    m_vertex_input;
    std::span<std::byte>                  m_slot_span;

    std::optional<Buffer_range>           m_vertex_buffer_range;
    std::size_t                           m_vertex_write_offset{0};

    // NOTE: Elements in m_buckets must be stable, etl::vector<> works, std::vector<> does not work.
    etl::vector<Line_renderer_bucket, 32> m_buckets;
    bool                                  m_is_opened{false};
    bool                                  m_is_closed{false};

    static constexpr std::size_t s_view_stride    = 256;
    static constexpr std::size_t s_max_view_count = 16;
    static constexpr std::size_t s_max_line_count = 64 * 1024;
};

} // namespace erhe::renderer
