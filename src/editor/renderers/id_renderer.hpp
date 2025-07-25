#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

typedef struct __GLsync *GLsync;

namespace erhe::graphics {
    class Render_pass;
    class Gpu_timer;
    class Device;
    class Renderbuffer;
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
    static constexpr std::size_t s_frame_resources_count = 4;
    static constexpr std::size_t s_extent                = 64;
    static constexpr std::size_t s_id_buffer_size        = s_extent * s_extent * 8; // RGBA + depth

    class Id_frame_resources
    {
    public:
        enum class State : unsigned int {
            Unused = 0,
            Waiting_for_read,
            Read_complete
        };

        Id_frame_resources(erhe::graphics::Device& graphics_device, const std::size_t slot);

        Id_frame_resources(const Id_frame_resources& other) = delete;
        auto operator=    (const Id_frame_resources&) -> Id_frame_resources& = delete;

        Id_frame_resources(Id_frame_resources&& other) noexcept;
        auto operator=(Id_frame_resources&& other) noexcept -> Id_frame_resources&;

        erhe::graphics::Buffer                pixel_pack_buffer;
        std::array<uint8_t, s_id_buffer_size> data           {};
        GLsync                                sync           {0};
        glm::mat4                             clip_from_world{1.0f};
        int                                   x_offset       {0};
        int                                   y_offset       {0};
        uint64_t                              frame_number   {0};
        State                                 state          {State::Unused};
    };

    [[nodiscard]] auto current_id_frame_resources() -> Id_frame_resources&;
    void create_id_frame_resources();
    void update_framebuffer       (const erhe::math::Viewport viewport);

    bool                                          m_enabled{true};
    erhe::math::Viewport                          m_viewport{0, 0, 0, 0};

    // TODO Do not store these here?
    erhe::graphics::Device&                       m_graphics_device;
    Mesh_memory&                                  m_mesh_memory;

    erhe::scene_renderer::Camera_buffer           m_camera_buffers;
    erhe::renderer::Draw_indirect_buffer          m_draw_indirect_buffers;
    erhe::scene_renderer::Primitive_buffer        m_primitive_buffers;

    erhe::graphics::Render_pipeline_state         m_pipeline;
    erhe::graphics::Render_pipeline_state         m_selective_depth_clear_pipeline;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_color_renderbuffer;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::unique_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>      m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass>  m_render_pass;
    std::vector<Id_frame_resources>               m_id_frame_resources;
    std::size_t                                   m_current_id_frame_resource_slot{0};
    erhe::graphics::Gpu_timer                     m_gpu_timer;

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
    bool               m_use_scissor      {true};
    bool               m_use_renderbuffers{true};
    bool               m_use_textures     {false};
};

}
