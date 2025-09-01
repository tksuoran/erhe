#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

typedef struct __GLsync *GLsync;

namespace erhe::graphics {
    class Render_pass;
    class Gpu_timer;
    class Device;
    class Texture;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace editor {

class Programs;
class Mesh_memory;

class Id_renderer final
{
public:
    bool enabled{true};

    class Id_query_result
    {
    public:
        uint32_t                           id             {0};
        float                              depth          {0.0f};
        std::shared_ptr<erhe::scene::Mesh> mesh           {};
        std::size_t                        index_of_gltf_primitive_in_mesh{0};
        std::size_t                        triangle_id    {std::numeric_limits<std::size_t>::max()};
        bool                               valid          {false};
        uint64_t                           frame_number   {0};
    };

    Id_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::scene_renderer::Program_interface& program_interface,
        Mesh_memory&                             mesh_memory,
        Programs&                                programs
    );
    ~Id_renderer();

    // Public API
    class Render_parameters
    {
    public:
        const erhe::math::Viewport&  viewport;
        const erhe::scene::Camera&   camera;
        const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>& content_mesh_spans;
        const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>& tool_mesh_spans;
        const int                    x;
        const int                    y;
    };
    void render(const Render_parameters& parameters);
    void next_frame();

    [[nodiscard]] auto get(const int x, const int y, uint32_t& out_id, float& out_depth, uint64_t& out_frame_number) -> bool;
    [[nodiscard]] auto get(const int x, const int y) -> Id_query_result;


private:
    static constexpr int s_transfer_entry_count = 4;
    static constexpr int s_extent               = 256; // Bigger value handle faster mouse speeds

    class Transfer_entry
    {
    public:
        enum class State : unsigned int {
            Unused = 0,
            Waiting_for_read,
            Read_complete
        };

        erhe::graphics::Ring_buffer_range buffer_range;
        std::vector<uint8_t>              data           {};
        glm::mat4                         clip_from_world{1.0f};
        int                               x_offset       {0};
        int                               y_offset       {0};
        uint64_t                          frame_number   {0};
        int                               slot           {0};
        State                             state          {State::Unused};
    };

    [[nodiscard]] auto get_current_transfer_entry() -> Transfer_entry&;

    void update_framebuffer(const erhe::math::Viewport viewport);

    bool                                         m_enabled{true};
    erhe::math::Viewport                         m_viewport{0, 0, 0, 0};

    // TODO Do not store these here?
    erhe::graphics::Device&                      m_graphics_device;
    Mesh_memory&                                 m_mesh_memory;

    erhe::scene_renderer::Camera_buffer          m_camera_buffers;
    erhe::renderer::Draw_indirect_buffer         m_draw_indirect_buffers;
    erhe::scene_renderer::Primitive_buffer       m_primitive_buffers;

    erhe::graphics::Render_pipeline_state        m_pipeline;
    erhe::graphics::Render_pipeline_state        m_selective_depth_clear_pipeline;
    std::unique_ptr<erhe::graphics::Texture>     m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
    erhe::graphics::Ring_buffer_client           m_texture_read_buffer;

    std::array<Transfer_entry, s_transfer_entry_count> m_transfer_entries;
    int                                                m_current_transfer_entry_slot{0};
    erhe::graphics::Gpu_timer                          m_gpu_timer;

    class Range
    {
    public:
        uint32_t                           offset   {0};
        uint32_t                           length   {0};
        std::shared_ptr<erhe::scene::Mesh> mesh     {nullptr};
        std::size_t                        mesh_primitive_index{0};
    };

    void render(erhe::graphics::Render_command_encoder& render_encoder, const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes);

    std::vector<Range> m_ranges;
    bool               m_use_scissor{true};
};

}
