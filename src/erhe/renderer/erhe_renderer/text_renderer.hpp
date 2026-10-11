#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_ui/rectangle.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include <optional>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class OpenGL_state_tracker;
    class Render_command_encoder;
    class Render_pass;
    class Sampler;
    class Shader_monitor;
    class Shader_stages;
    class Texture_heap;
}
namespace erhe::ui {
    class Font;
}

namespace erhe::renderer {

class Text_renderer
{
public:
    class Config
    {
    public:
        bool enabled{true};
        int  font_size{14};
    };
    Config config;

    // view_count: when >= 2, build an additional multiview-compiled
    // Shader_stages variant so render(..., multiview = true) can draw
    // the same screen-space text into a multiview render pass and have
    // it appear on every layer in one draw. Default 1 keeps existing
    // single-view callers unchanged. Mirrors the pattern used by
    // Content_wide_line_renderer / Debug_renderer.
    Text_renderer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        bool                            enabled        = true,
        int                             font_size      = 14,
        int                             view_count = 1
    );
    ~Text_renderer() noexcept;

    Text_renderer (const Text_renderer&) = delete;
    void operator=(const Text_renderer&) = delete;
    Text_renderer (Text_renderer&&)      = delete;
    void operator=(Text_renderer&&)      = delete;

    // Public API. Prints are tagged with the current frame slot
    // (set_frame_slot(), the same ids Debug_renderer::begin_frame() returns,
    // so a view's text and lines are recorded and drawn together); render()
    // draws one slot and releases its buffers; release_frames() releases
    // whatever was recorded but not drawn, once per device frame.
    //
    // Frames in flight: the prints and the current slot are kept once per
    // device frame in flight (Device::get_number_of_frames_in_flight()) and
    // every entry point works on the frame of the role-keyed
    // Device::get_frame_index(), as in Debug_renderer: the main thread
    // prints into the frame it prepares while the render thread draws and
    // releases the frame it renders. The slot is 0 at the first print or
    // set_frame_slot() of each device frame.
    void set_frame_slot(std::size_t frame_slot);
    void print(glm::vec3 text_position, uint32_t text_color, std::string_view text);
    [[nodiscard]] auto font_size() -> float;
    [[nodiscard]] auto measure  (std::string_view text) const -> erhe::ui::Rectangle;

    void render(
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::graphics::Render_pass&      render_pass,
        erhe::math::Viewport                    viewport,
        bool                                    multiview,
        std::size_t                             frame_slot
    );
    void release_frames();

private:
    auto build_shader_stages          () -> erhe::graphics::Shader_stages_prototype;
    auto build_multiview_shader_stages() -> erhe::graphics::Shader_stages_prototype;

    static constexpr std::size_t s_vertex_count{65536 * 8};

    static constexpr std::size_t uint16_max              {65535};
    static constexpr std::size_t uint16_primitive_restart{0xffffu};
    static constexpr std::size_t per_quad_vertex_count   {4}; // corner count
    static constexpr std::size_t per_quad_index_count    {per_quad_vertex_count + 1}; // Plus one for primitive restart
    static constexpr std::size_t max_quad_count          {uint16_max / per_quad_vertex_count}; // each quad consumes 4 indices
    static constexpr std::size_t index_count             {uint16_max * per_quad_index_count};
    static constexpr std::size_t index_stride            {2};

    erhe::graphics::Device&                m_graphics_device;
    int                                    m_view_count{1};
    erhe::graphics::Shader_resource        m_projection_block;
    erhe::graphics::Shader_resource        m_vertex_ssbo_block;
    erhe::graphics::Shader_resource*       m_clip_from_window_resource;
    erhe::graphics::Shader_resource*       m_texture_resource;
    erhe::graphics::Shader_resource*       m_vertex_data_resource;
    std::size_t                            m_u_clip_from_window_size        {0};
    std::size_t                            m_u_clip_from_window_offset      {0};
    std::size_t                            m_u_texture_size                 {0};
    std::size_t                            m_u_texture_offset               {0};
    std::size_t                            m_u_vertex_data_size             {0};
    std::size_t                            m_u_vertex_data_offset           {0};
    erhe::graphics::Fragment_outputs       m_fragment_outputs;
    erhe::graphics::Sampler                m_nearest_sampler;
    erhe::graphics::Bind_group_layout      m_bind_group_layout;
    erhe::graphics::Shader_stages          m_shader_stages;
    std::optional<erhe::graphics::Shader_stages> m_multiview_shader_stages;
    std::unique_ptr<erhe::ui::Font>        m_font;
    erhe::graphics::Ring_buffer_client     m_vertex_ssbo_buffer;
    erhe::graphics::Ring_buffer_client     m_projection_buffer;
    erhe::graphics::Vertex_input_state     m_vertex_input;
    erhe::graphics::Base_render_pipeline   m_pipeline;

    class Vertex_range
    {
    public:
        std::size_t                       frame_slot;
        erhe::graphics::Ring_buffer_range range;
    };
    // The prints of one device frame in flight.
    class Frame
    {
    public:
        uint64_t                  frame_index         {std::numeric_limits<uint64_t>::max()}; // device frame printed into this entry
        std::size_t               frame_slot          {0};
        std::vector<Vertex_range> vertex_buffer_ranges{};
    };
    // The entry of the calling role's device frame, claimed for that frame
    // (verifying the frame that used it before released its prints).
    [[nodiscard]] auto get_recording_frame() -> Frame&;

    // One entry per device frame in flight, indexed by frame index % size;
    // sized at construction, never resized.
    std::vector<Frame>                             m_frames;
    std::unique_ptr<erhe::graphics::Texture_heap>  m_texture_heap;
};

} // namespace erhe::renderer
