#pragma once

#include "erhe/components/component.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/scene/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::graphics
{
    class Framebuffer;
}

namespace editor
{

class Framebuffer_capture
    : public erhe::components::Component,
{
public:
    Framebuffer_capture();

    virtual ~Framebuffer_capture() = default;

    void initialize_component() override;

    void capture(erhe::scene::Viewport viewport);

private:
    static constexpr size_t s_frame_resources_count = 4;

    class Framebuffer_capture_resources
    {
    public:
        enum class State
        {
            unused           = 0,
            waiting_for_read = 1,
            read_complete    = 2
        };

        Framebuffer_capture_resources()
            : pixel_pack_buffer{gl::Buffer_target::pixel_pack_buffer, s_id_buffer_size, storage_mask, access_mask}
        {
            pixel_pack_buffer.set_debug_label("ID Renderer Pixel Pack");
        }

        Framebuffer_capture_resources(const Framebuffer_capture_resources& other) = delete;
        auto operator=(const Framebuffer_capture_resources&) -> Framebuffer_capture_resources& = delete;

        Framebuffer_capture_resources(Framebuffer_capture_resources&& other) noexcept
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

        auto operator=(Framebuffer_capture_resources&& other) noexcept -> Framebuffer_capture_resources&
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

        erhe::graphics::Buffer     pixel_pack_buffer;
        int                        framebuffer;
        gl::Framebuffer_attachment attachment;
        int                        object_type;
        int                        gl_name;
        gl::Pixel_format           pixel_format;
        gl::Pixel_type             pixel_type;
        int                        width;
        int                        height;
        int                        size;
        std::array<uint8_t, s_id_buffer_size> data;
        double                                time{0.0};
        GLsync                                sync{0};
        int                                   x_offset{0};
        int                                   y_offset{0};
        State                                 state{State::unused};
    };

    void create_frame_resources();

    auto current_frame_resources() -> Id_frame_resources&;

    erhe::scene::Viewport           m_viewport;
    erhe::graphics::Pipeline        m_pipeline;
    erhe::graphics::Pipeline        m_selective_depth_clear_pipeline;
    std::vector<Id_frame_resources> m_frame_resources;
    size_t                          m_current_id_frame_resource_slot{0};
};

}
