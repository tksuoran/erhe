#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"
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

class Debug_renderer_program_interface
{
public:
    explicit Debug_renderer_program_interface(erhe::graphics::Instance& graphics_instance);

    bool                                             reverse_depth{false};
    erhe::graphics::Fragment_outputs                 fragment_outputs;
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

class Primitive_renderer;
class Debug_renderer_bucket;
class Debug_renderer_config;

class Debug_renderer
{
public:
    Debug_renderer(erhe::graphics::Instance& graphics_instance);
    ~Debug_renderer();

    // Public API
    auto get   (const Debug_renderer_config& config) -> Primitive_renderer;
    auto get   (unsigned int stencil, bool visible, bool hidden) -> Primitive_renderer;
    void render(const erhe::math::Viewport camera_viewport, const erhe::scene::Camera& camera);

    // API for Debug_renderer_bucket
    auto get_program_interface() const -> const Debug_renderer_program_interface& { return m_program_interface; }
    auto get_vertex_input     () -> erhe::graphics::Vertex_input_state* { return &m_vertex_input ;}

private:
    [[nodiscard]] auto update_view_buffer(
        const erhe::math::Viewport viewport,
        const erhe::scene::Camera& camera
    ) -> erhe::graphics::Buffer_range;

    erhe::graphics::Instance&              m_graphics_instance;
    Debug_renderer_program_interface       m_program_interface;

    erhe::graphics::GPU_ring_buffer_client m_view_buffer;
    erhe::graphics::Vertex_input_state     m_vertex_input;

    // NOTE: Elements in m_buckets must be stable, etl::vector<> works, std::vector<> does not work.
    etl::vector<Debug_renderer_bucket, 32> m_buckets;
};

} // namespace erhe::renderer
