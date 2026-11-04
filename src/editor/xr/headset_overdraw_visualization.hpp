#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"

#include <cstdint>

namespace erhe::graphics {
    class Buffer;
    class Device;
    class Render_command_encoder;
    class Render_pass;
}
namespace erhe::scene_renderer {
    class Mesh_memory;
}

namespace editor {

enum class Overdraw_ramp : uint32_t; // scene/viewport_render_state.hpp

// The headset's overdraw heat map (doc/plans/overdraw_visualization.md
// "Headset"): one fullscreen multiview draw over the eye images, inside a
// multiview render pass after the content passes, reading the headset's
// count buffer (Overdraw_view_state; view v's rows follow view v - 1's).
// Premultiplied blend: counted pixels show the ramp, uncounted pixels keep
// a quarter of the scene color.
class Headset_overdraw_visualization
{
public:
    Headset_overdraw_visualization(erhe::graphics::Device& graphics_device, uint32_t view_count);
    ~Headset_overdraw_visualization() noexcept;
    Headset_overdraw_visualization(const Headset_overdraw_visualization&)            = delete;
    Headset_overdraw_visualization& operator=(const Headset_overdraw_visualization&) = delete;

    // Render phase, inside render_pass (a multiview pass of view_count
    // views). The count buffer's counting writes are made visible to the
    // fragment stage before the pass (a barrier outside any render pass).
    void draw(
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::graphics::Render_pass&      render_pass,
        erhe::scene_renderer::Mesh_memory&      mesh_memory,
        const erhe::graphics::Buffer&           count_buffer,
        uint32_t                                width,
        uint32_t                                height,
        uint32_t                                max_count,
        Overdraw_ramp                           ramp
    );

private:
    static constexpr unsigned int counts_binding_point    {0};
    static constexpr unsigned int parameters_binding_point{1};

    erhe::graphics::Device&                  m_graphics_device;
    erhe::graphics::Shader_resource          m_counts_block;
    erhe::graphics::Shader_resource          m_parameters_block;
    erhe::graphics::Bind_group_layout        m_bind_group_layout;
    erhe::graphics::Fragment_outputs         m_fragment_outputs;
    erhe::graphics::Reloadable_shader_stages m_shader_stages;
    erhe::graphics::Base_render_pipeline     m_pipeline;
    erhe::graphics::Ring_buffer_client       m_parameters_buffer;
};

}
