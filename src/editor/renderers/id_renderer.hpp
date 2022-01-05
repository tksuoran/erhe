#pragma once

#include "renderers/base_renderer.hpp"

#include "erhe/components/component.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Renderbuffer;
    class Texture;
}

namespace erhe::scene
{
    class Camera;
    class Mesh;
    class Mesh_layer;
}

namespace editor
{

class Mesh_memory;
class Programs;

class Id_renderer
    : public erhe::components::Component,
      public Base_renderer
{
public:
    class Mesh_primitive
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh                {};
        const erhe::scene::Mesh_layer*     layer               {nullptr};
        size_t                             mesh_primitive_index{0};
        size_t                             local_index         {0};
        bool                               valid               {false};
    };

    static constexpr std::string_view c_name{"Id_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Id_renderer ();
    ~Id_renderer() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    void render(
        const erhe::scene::Viewport  viewport,
        const Mesh_layer_collection& content_mesh_layers,
        const Mesh_layer_collection& tool_mesh_layers,
        erhe::scene::ICamera&        camera,
        const double                 time,
        const int                    x,
        const int                    y
    );

    [[nodiscard]]
    auto get(const int x, const int y, uint32_t& id, float& depth) -> bool;

    [[nodiscard]]
    auto get(const int x, const int y, float& depth) -> Mesh_primitive;

    void next_frame();

private:
    static constexpr size_t s_frame_resources_count = 4;
    static constexpr size_t s_extent                = 64;
    static constexpr size_t s_id_buffer_size        = s_extent * s_extent * 8; // RGBA + depth

    class Id_frame_resources
    {
    public:
        static constexpr gl::Buffer_storage_mask storage_mask{
            gl::Buffer_storage_mask::map_coherent_bit   |
            gl::Buffer_storage_mask::map_persistent_bit |
            gl::Buffer_storage_mask::map_read_bit
        };

        static constexpr gl::Map_buffer_access_mask access_mask{
            gl::Map_buffer_access_mask::map_coherent_bit   |
            gl::Map_buffer_access_mask::map_persistent_bit |
            gl::Map_buffer_access_mask::map_read_bit
        };
        enum class State : unsigned int
        {
            Unused = 0,
            Waiting_for_read,
            Read_complete
        };

        explicit Id_frame_resources(const size_t slot)
            : pixel_pack_buffer{
                gl::Buffer_target::pixel_pack_buffer,
                s_id_buffer_size,
                storage_mask,
                access_mask
            }
        {
            pixel_pack_buffer.set_debug_label(fmt::format("ID Pixel Pack {}", slot));
        }

        Id_frame_resources(const Id_frame_resources& other) = delete;
        auto operator=    (const Id_frame_resources&) -> Id_frame_resources& = delete;

        Id_frame_resources(Id_frame_resources&& other) noexcept
            : pixel_pack_buffer{std::move(other.pixel_pack_buffer)}
            , data             {std::move(other.data)}
            , time             {other.time}
            , sync             {other.sync}
            , clip_from_world  {other.clip_from_world}
            , x_offset         {other.x_offset}
            , y_offset         {other.y_offset}
            , state            {other.state}
        {
        }

        auto operator=(Id_frame_resources&& other) noexcept -> Id_frame_resources&
        {
            pixel_pack_buffer = std::move(other.pixel_pack_buffer);
            data              = std::move(other.data);
            time              = other.time;
            sync              = other.sync;
            clip_from_world   = other.clip_from_world;
            x_offset          = other.x_offset;
            y_offset          = other.y_offset;
            state             = other.state;
            return *this;
        }

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
    void render_layer             (const erhe::scene::Mesh_layer& layer);

    erhe::scene::Viewport                 m_viewport{0, 0, 0, 0, true};

    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;

    erhe::graphics::Pipeline                              m_pipeline;
    erhe::graphics::Pipeline                              m_selective_depth_clear_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state>   m_vertex_input;
    std::unique_ptr<erhe::graphics::Renderbuffer>         m_color_renderbuffer;
    std::unique_ptr<erhe::graphics::Renderbuffer>         m_depth_renderbuffer;
    std::unique_ptr<erhe::graphics::Texture>              m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>              m_depth_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>          m_framebuffer;
    std::vector<Id_frame_resources>                       m_id_frame_resources;
    size_t                                                m_current_id_frame_resource_slot{0};

    class Range
    {
    public:
        uint32_t                           offset   {0};
        uint32_t                           length   {0};
        std::shared_ptr<erhe::scene::Mesh> mesh     {nullptr};
        size_t                             mesh_primitive_index{0};
    };

    class Layer_range
    {
    public:
        uint32_t                       offset{0};
        uint32_t                       end   {0};
        const erhe::scene::Mesh_layer* layer {nullptr};
    };

    std::vector<Range>       m_ranges;
    std::vector<Layer_range> m_layer_ranges;
    bool                     m_use_scissor      {true};
    bool                     m_use_renderbuffers{true};
    bool                     m_use_textures     {false};
};

}
