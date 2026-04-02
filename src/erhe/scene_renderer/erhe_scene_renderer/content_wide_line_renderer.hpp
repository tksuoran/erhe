#pragma once

#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Compute_command_encoder;
    class Device;
    class Render_command_encoder;
    class Shader_stages;
}
namespace erhe::primitive {
    class Buffer_mesh;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
    class Node;
}

namespace erhe::scene_renderer {

// Dispatches one compute per mesh primitive, expanding edge line
// vertex pairs to triangle quads for wide line rendering.
// Used on Metal (and other backends without geometry shaders).
//
// The app provides compute_shader_stages and graphics_shader_stages.
// The compute shader must match the interface defined by this class
// (edge_line_vertex_buffer SSBO, triangle_vertex_buffer SSBO, view UBO).
class Content_wide_line_renderer
{
public:
    Content_wide_line_renderer(
        erhe::graphics::Device&        graphics_device,
        erhe::graphics::Buffer&        edge_line_vertex_buffer,
        erhe::graphics::Shader_stages* compute_shader_stages,
        erhe::graphics::Shader_stages* graphics_shader_stages
    );
    ~Content_wide_line_renderer() noexcept;

    [[nodiscard]] auto is_enabled() const -> bool;

    // Set shader stages after construction (for two-phase init)
    void set_shader_stages(
        erhe::graphics::Shader_stages* compute_shader_stages,
        erhe::graphics::Shader_stages* graphics_shader_stages
    );

    // Shader resource accessors - app needs these to build shader create_info
    [[nodiscard]] auto get_edge_line_vertex_struct      () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_edge_line_vertex_buffer_block() const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_triangle_vertex_struct       () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_triangle_vertex_buffer_block () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_view_block                   () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_fragment_outputs             () -> erhe::graphics::Fragment_outputs&;
    [[nodiscard]] auto get_triangle_vertex_format       () -> erhe::dataformat::Vertex_format&;
    [[nodiscard]] auto get_vertex_input                 () -> erhe::graphics::Vertex_input_state*;
    [[nodiscard]] auto get_graphics_shader_stages       () -> erhe::graphics::Shader_stages*;

    // Per-frame lifecycle
    void begin_frame();

    // Add edge lines from a mesh for compute expansion.
    // group identifies which render pass this mesh belongs to.
    void add_mesh(
        const erhe::scene::Mesh& mesh,
        const glm::vec4&         color,
        float                    line_width,
        uint32_t                 group = 0
    );

    // Dispatch compute shader to expand lines to triangles (call before render pass)
    void compute(
        erhe::graphics::Compute_command_encoder& command_encoder,
        const erhe::math::Viewport&              viewport,
        const erhe::scene::Camera&               camera,
        bool                                     reverse_depth,
        erhe::math::Depth_range                  depth_range,
        erhe::math::Framebuffer_origin           framebuffer_origin = erhe::math::Framebuffer_origin::bottom_left,
        erhe::math::Ndc_y_direction              ndc_y_direction    = erhe::math::Ndc_y_direction::up
    );

    // Render the expanded triangles for a specific group (call inside render pass)
    void render(
        erhe::graphics::Render_command_encoder&      render_encoder,
        const erhe::graphics::Render_pipeline_state&  pipeline_state,
        uint32_t                                      group = 0
    );

    void end_frame();

private:
    struct Dispatch_entry
    {
        std::size_t edge_buffer_byte_offset{0};
        std::size_t edge_count             {0};
        glm::mat4   world_from_node        {1.0f};
        glm::vec4   color                  {1.0f};
        float       line_width             {1.0f};
        uint32_t    group                  {0};

        // Filled by compute()
        erhe::graphics::Ring_buffer_range triangle_buffer_range{};
        bool                              dispatched{false};
    };

    erhe::graphics::Device&                                m_graphics_device;
    erhe::graphics::Buffer&                                m_edge_line_vertex_buffer;

    // Shader resources (define the interface contract for shaders)
    erhe::graphics::Fragment_outputs                       m_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_edge_line_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_edge_line_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_triangle_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_triangle_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_view_block;
    erhe::dataformat::Vertex_format                        m_triangle_vertex_format;

    // Offsets within view UBO
    std::size_t                                            m_clip_from_world_offset{0};
    std::size_t                                            m_world_from_node_offset{0};
    std::size_t                                            m_viewport_offset       {0};
    std::size_t                                            m_fov_offset            {0};
    std::size_t                                            m_line_color_offset     {0};
    std::size_t                                            m_edge_count_offset    {0};
    std::size_t                                            m_vp_y_sign_offset            {0};
    std::size_t                                            m_clip_depth_direction_offset  {0};
    std::size_t                                            m_padding0_offset             {0};
    std::size_t                                            m_padding1_offset             {0};
    std::size_t                                            m_view_position_in_world_offset{0};

    // Shader stages provided by app
    erhe::graphics::Shader_stages*                         m_compute_shader_stages {nullptr};
    erhe::graphics::Shader_stages*                         m_graphics_shader_stages{nullptr};
    std::optional<erhe::graphics::Compute_pipeline_state>  m_compute_pipeline;
    erhe::graphics::Vertex_input_state                     m_vertex_input;

    // Ring buffer clients
    erhe::graphics::Ring_buffer_client                     m_view_buffer;
    erhe::graphics::Ring_buffer_client                     m_triangle_vertex_buffer_client;

    // Per-frame state
    std::vector<Dispatch_entry>                            m_dispatches;
    bool                                                   m_enabled{false};
};

} // namespace erhe::scene_renderer
