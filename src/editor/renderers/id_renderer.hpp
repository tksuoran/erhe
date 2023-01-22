#pragma once

#include "renderers/camera_buffer.hpp"
#include "renderers/draw_indirect_buffer.hpp"
#include "renderers/primitive_buffer.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <memory>
#include <vector>

typedef struct __GLsync *GLsync;

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
    class Renderbuffer;
    class Texture;
}

namespace erhe::scene
{
    class Camera;
    class Mesh;
}

namespace editor
{

class Id_renderer
    : public erhe::components::Component
{
public:
    class Id_query_result
    {
    public:
        uint32_t                           id                  {0};
        float                              depth               {0.0f};
        std::shared_ptr<erhe::scene::Mesh> mesh                {};
        std::size_t                        mesh_primitive_index{0};
        std::size_t                        local_index         {0};
        bool                               valid               {false};
    };

    static constexpr std::string_view c_type_name{"Id_renderer"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Id_renderer ();
    ~Id_renderer() noexcept override;

    // Implements Component
    auto get_type_hash              () const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    class Render_parameters
    {
    public:
        const erhe::scene::Viewport& viewport;
        const erhe::scene::Camera*   camera;
        const std::initializer_list<const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>>& content_mesh_spans;
        const std::initializer_list<const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>>& tool_mesh_spans;
        const double                 time;
        const int                    x;
        const int                    y;
    };
    void render(const Render_parameters& parameters);

    [[nodiscard]] auto get(const int x, const int y, uint32_t& id, float& depth) -> bool;
    [[nodiscard]] auto get(const int x, const int y) -> Id_query_result;

    void next_frame();

private:
    static constexpr std::size_t s_frame_resources_count = 4;
    static constexpr std::size_t s_extent                = 256;
    static constexpr std::size_t s_id_buffer_size        = s_extent * s_extent * 8; // RGBA + depth

    class Id_frame_resources
    {
    public:
        enum class State : unsigned int
        {
            Unused = 0,
            Waiting_for_read,
            Read_complete
        };

        explicit Id_frame_resources(const std::size_t slot);

        Id_frame_resources(const Id_frame_resources& other) = delete;
        auto operator=    (const Id_frame_resources&) -> Id_frame_resources& = delete;

        Id_frame_resources(Id_frame_resources&& other) noexcept;
        auto operator=(Id_frame_resources&& other) noexcept -> Id_frame_resources&;

        erhe::graphics::Buffer                pixel_pack_buffer;
        std::array<uint8_t, s_id_buffer_size> data;
        double                                time           {0.0};
        GLsync                                sync           {0};
        glm::mat4                             clip_from_world{1.0f};
        int                                   x_offset       {0};
        int                                   y_offset       {0};
        State                                 state          {State::Unused};
    };

    [[nodiscard]] auto current_id_frame_resources() -> Id_frame_resources&;
    void create_id_frame_resources();
    void update_framebuffer       (const erhe::scene::Viewport viewport);

    erhe::scene::Viewport                         m_viewport{0, 0, 0, 0, true};

    erhe::graphics::Pipeline                      m_pipeline;
    erhe::graphics::Pipeline                      m_selective_depth_clear_pipeline;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_color_renderbuffer;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::unique_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>      m_depth_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>  m_framebuffer;
    std::vector<Id_frame_resources>               m_id_frame_resources;
    std::size_t                                   m_current_id_frame_resource_slot{0};
    std::unique_ptr<erhe::graphics::Gpu_timer>    m_gpu_timer;

    class Range
    {
    public:
        uint32_t                           offset   {0};
        uint32_t                           length   {0};
        std::shared_ptr<erhe::scene::Mesh> mesh     {nullptr};
        std::size_t                        mesh_primitive_index{0};
    };

    void render(
        const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes
    );

    std::vector<Range> m_ranges;
    bool               m_use_scissor      {true};
    bool               m_use_renderbuffers{true};
    bool               m_use_textures     {false};

    std::unique_ptr<Camera_buffer       > m_camera_buffers;
    std::unique_ptr<Draw_indirect_buffer> m_draw_indirect_buffers;
    std::unique_ptr<Primitive_buffer    > m_primitive_buffers;
};

extern Id_renderer* g_id_renderer;

}
