#pragma once

#include "renderers/base_renderer.hpp"
#include "renderers/programs.hpp"

#include "erhe/components/component.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/scene/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::graphics
{
    class OpenGL_state_tracker;
}

namespace erhe::scene
{
    class Camera;
    class Mesh;
}

namespace editor
{

class Scene_manager;

class Id_renderer
    : public erhe::components::Component,
      public Base_renderer
{
public:
    struct Mesh_primitive
    {
        std::shared_ptr<erhe::scene::Mesh> mesh                {nullptr};
        erhe::scene::Layer*                layer               {nullptr};
        size_t                             mesh_primitive_index{0};
        size_t                             local_index         {0};
        bool                               valid               {false};
    };

    static constexpr const char* c_name = "Id_renderer";
    Id_renderer();

    virtual ~Id_renderer() = default;

    // Implements Component
    void connect() override;
    void initialize_component() override;

    void render(erhe::scene::Viewport   viewport,
                const Layer_collection& content_layers,
                const Layer_collection& tool_layers,
                erhe::scene::ICamera&   camera,
                double                  time,
                int                     x,
                int                     y);

    auto get(int x, int y, uint32_t& id, float& depth) -> bool;

    auto get(int x, int y, float& depth) -> Mesh_primitive;

    void next_frame();

private:
    static constexpr size_t s_frame_resources_count = 4;
    static constexpr size_t s_extent                = 64;
    static constexpr size_t s_id_buffer_size        = s_extent * s_extent * 8; // RGBA + depth

    struct Id_frame_resources
    {
        static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_coherent_bit   |
                                                              gl::Buffer_storage_mask::map_persistent_bit |
                                                              gl::Buffer_storage_mask::map_read_bit};

        static constexpr gl::Map_buffer_access_mask access_mask{gl::Map_buffer_access_mask::map_coherent_bit   |
                                                                gl::Map_buffer_access_mask::map_persistent_bit |
                                                                gl::Map_buffer_access_mask::map_read_bit};
        enum class State : unsigned int
        {
            unused = 0,
            waiting_for_read,
            read_complete
        };

        Id_frame_resources()
            : pixel_pack_buffer{gl::Buffer_target::pixel_pack_buffer, s_id_buffer_size, storage_mask, access_mask}
        {
            pixel_pack_buffer.set_debug_label("ID Renderer Pixel Pack");
        }

        Id_frame_resources(const Id_frame_resources& other) = delete;
        auto operator=(const Id_frame_resources&) -> Id_frame_resources& = delete;

        Id_frame_resources(Id_frame_resources&& other) noexcept
        {
            pixel_pack_buffer = std::move(other.pixel_pack_buffer);
            data              = std::move(other.data             );
            time              = other.time;
            sync              = other.sync;
            clip_from_world   = other.clip_from_world;
            x_offset          = other.x_offset;
            y_offset          = other.y_offset;
            state             = other.state;
        }

        auto operator=(Id_frame_resources&& other) noexcept -> Id_frame_resources&
        {
            pixel_pack_buffer = std::move(other.pixel_pack_buffer);
            data              = std::move(other.data             );
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
        double                                time{0.0};
        GLsync                                sync{0};
        glm::mat4                             clip_from_world{1.0f};
        int                                   x_offset{0};
        int                                   y_offset{0};
        State                                 state{State::unused};
    };

    void create_id_frame_resources();

    auto current_id_frame_resources() -> Id_frame_resources&;

    void update_framebuffer(erhe::scene::Viewport viewport);

    void render_layer(erhe::scene::Layer* layer);

    erhe::scene::Viewport                                 m_viewport;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Scene_manager>                        m_scene_manager;
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

    struct Range
    {
        uint32_t                           offset   {0};
        uint32_t                           length   {0};
        std::shared_ptr<erhe::scene::Mesh> mesh     {nullptr};
        size_t                             mesh_primitive_index{0};
    };

    struct Layer_range
    {
        uint32_t            offset{0};
        uint32_t            end   {0};
        erhe::scene::Layer* layer {nullptr};
    };

    std::vector<Range>       m_ranges;
    std::vector<Layer_range> m_layer_ranges;
    bool                     m_use_scissor      {true};
    bool                     m_use_renderbuffers{true};
    bool                     m_use_textures     {false};
};

}
