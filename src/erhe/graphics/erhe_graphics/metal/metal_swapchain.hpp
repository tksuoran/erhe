#pragma once

#include "erhe_graphics/swapchain.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CA  { class MetalDrawable; }
namespace MTL { class Buffer; }
namespace MTL { class CommandBuffer; }
namespace MTL { class Texture; }

namespace erhe::graphics {

class Device_impl;
class Surface_impl;

class Swapchain_impl final
{
public:
    Swapchain_impl(
        Device_impl&  device_impl,
        Surface_impl& surface_impl
    );
    ~Swapchain_impl() noexcept;

    Swapchain_impl (const Swapchain_impl&) = delete;
    Swapchain_impl& operator=(const Swapchain_impl&) = delete;
    Swapchain_impl (Swapchain_impl&&) = delete;
    Swapchain_impl& operator=(Swapchain_impl&&) = delete;

    [[nodiscard]] auto wait_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;
    [[nodiscard]] auto has_depth       () const -> bool;
    [[nodiscard]] auto has_stencil    () const -> bool;
    [[nodiscard]] auto get_color_format() const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format() const -> erhe::dataformat::Format;

    [[nodiscard]] auto get_current_drawable() const -> CA::MetalDrawable*;

    // Color attachment 0 of the swapchain render pass: the current
    // drawable's texture in the windowed configuration, the current
    // emulated ring texture in the headless one. nullptr when no frame is
    // in flight.
    [[nodiscard]] auto get_current_color_texture() const -> MTL::Texture*;

    // Called by Render_pass_impl::end_render_pass once the swapchain render
    // pass has been recorded: the current texture now holds the fully
    // composited frame, which is what the headless readback path reads.
    // Mirrors Emulated_swapchain_impl::mark_render_pass_recorded().
    void mark_render_pass_recorded();

    // Releases the reference taken in begin_frame(). Called by
    // Device_impl::submit_command_buffers once presentDrawable has been
    // encoded - the drawable must not be held past that point, or the
    // layer's small drawable pool runs dry. No-op when headless.
    void clear_current_drawable();

    // Screenshot capture (Device::capture_last_frame). Windowed: a drawable
    // is handed to the compositor at presentDrawable and cannot be read
    // afterwards, so capture is a one-shot arm-then-collect protocol, the
    // same as the Vulkan swapchain: request_capture() arms it; the
    // swapchain render pass epilogue (Render_pass_impl::end_render_pass)
    // calls record_capture(), which blits the freshly composited drawable
    // texture into a persistent shared MTL::Buffer on the frame's own
    // command buffer (before presentDrawable) and keeps a reference to
    // that command buffer; read_back_capture() waits for it to complete,
    // then returns the pixels as tightly packed opaque RGBA8. Supported
    // whenever the layer hands out readable drawables (Surface_impl sets
    // framebufferOnly = false on the layer for this).
    //
    // Headless: the emulated ring textures stay owned by this class and
    // stay readable, so there is nothing to arm - request_capture() is a
    // no-op and read_back_capture() blits the most recently composited ring
    // texture on a fresh command buffer and waits for it, synchronously.
    [[nodiscard]] auto is_capture_supported() const -> bool;
    void               request_capture();
    void               record_capture(MTL::CommandBuffer* command_buffer);
    [[nodiscard]] auto read_back_capture(uint32_t& out_width, uint32_t& out_height, std::vector<std::byte>& out_rgba8) -> bool;

private:
    // Emulated (headless) backbuffer: a ring of offscreen color textures
    // standing in for layer drawables. Created once at construction.
    void create_emulated_textures ();
    void release_emulated_textures();
    [[nodiscard]] auto is_headless() const -> bool;

    // Grows m_capture_buffer to at least byte_count bytes. Returns false
    // when the allocation fails.
    [[nodiscard]] auto ensure_capture_buffer(std::size_t byte_count) -> bool;

    // Converts the capture buffer contents (m_capture_width x
    // m_capture_height, m_capture_pixel_format) into tightly packed opaque
    // RGBA8.
    void convert_capture_buffer(std::vector<std::byte>& out_rgba8) const;

    // Headless readback: blit m_emulated_textures[m_last_composited_index]
    // into the capture buffer on a fresh command buffer and wait for it.
    [[nodiscard]] auto read_back_emulated(uint32_t& out_width, uint32_t& out_height, std::vector<std::byte>& out_rgba8) -> bool;

    Device_impl&        m_device_impl;
    Surface_impl&       m_surface_impl;
    CA::MetalDrawable*  m_current_drawable{nullptr};

    std::vector<MTL::Texture*> m_emulated_textures;      // empty unless headless
    std::size_t                m_emulated_index      {0};
    std::size_t                m_last_composited_index{0};
    bool                       m_have_composited      {false};
    uint32_t                   m_emulated_width       {0};
    uint32_t                   m_emulated_height      {0};

    bool                m_capture_requested     {false};
    MTL::Buffer*        m_capture_buffer        {nullptr}; // shared storage, width * height * 4 bytes
    MTL::CommandBuffer* m_capture_command_buffer{nullptr}; // retained until read back
    uint32_t            m_capture_width         {0};
    uint32_t            m_capture_height        {0};
    unsigned long       m_capture_pixel_format  {0};       // MTL::PixelFormat of the captured texture
    uint64_t            m_capture_frame_index   {0};
};

} // namespace erhe::graphics
