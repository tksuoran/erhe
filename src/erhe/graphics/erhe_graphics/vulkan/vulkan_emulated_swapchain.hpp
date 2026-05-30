#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#include <cstdint>
#include <vector>

namespace erhe::graphics {

class Device_impl;
class Surface_impl;
class Frame_state;
class Frame_begin_info;

// Standalone, surfaceless "swapchain" used by the headless Vulkan
// configuration (ERHE_WINDOW_LIBRARY=none). It allocates a small ring of
// offscreen color images (plus optional depth) that stand in for a real
// VkSwapchainKHR, so the rest of the engine can render frames without a
// VkSurfaceKHR / presentation engine.
//
// There is no acquire / present: image reuse is made safe by single-queue
// submission order, the render pass EXTERNAL dependency and the CLEAR
// (discard) load op. Frame pacing comes entirely from the device timeline
// semaphore (Device_impl::wait_frame), so this class never allocates
// acquire / present semaphores and never touches the legacy per-slot
// submit_fence.
//
// The color images are allocated VK_IMAGE_USAGE_TRANSFER_SRC_BIT and the
// swapchain render pass leaves them in TRANSFER_SRC_OPTIMAL, so a future
// readback / file export is a drop-in.
//
// This is a dedicated, self-contained sibling of the real WSI Swapchain_impl,
// which is intentionally left untouched. Some helper code (image view /
// framebuffer / depth creation) is duplicated on purpose; sharing it is out
// of scope for now.
class Emulated_swapchain_impl final
{
public:
    Emulated_swapchain_impl(Device_impl& device_impl, Surface_impl& surface_impl);
    ~Emulated_swapchain_impl() noexcept;

    Emulated_swapchain_impl(const Emulated_swapchain_impl&) = delete;
    Emulated_swapchain_impl& operator=(const Emulated_swapchain_impl&) = delete;
    Emulated_swapchain_impl(Emulated_swapchain_impl&&) = delete;
    Emulated_swapchain_impl& operator=(Emulated_swapchain_impl&&) = delete;

    [[nodiscard]] auto wait_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto present    () -> bool;

    // Consumed by Render_pass_impl: mirrors the subset of the Swapchain_impl
    // interface the swapchain render-pass branch uses.
    [[nodiscard]] auto acquire_swapchain_framebuffer(VkRenderPass render_pass) -> VkFramebuffer;
    [[nodiscard]] auto get_swapchain_extent() const -> VkExtent2D;
    void               mark_render_pass_recorded();
    [[nodiscard]] auto get_surface_impl    () -> Surface_impl&;
    [[nodiscard]] auto has_depth           () const -> bool;
    [[nodiscard]] auto has_stencil         () const -> bool;
    [[nodiscard]] auto get_color_format    () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format    () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_image_view() const -> VkImageView;
    [[nodiscard]] auto get_vk_depth_format () const -> VkFormat;

private:
    void create_images     ();
    void create_depth_image(uint32_t width, uint32_t height);
    void release_resources ();

    Device_impl&               m_device_impl;
    Surface_impl&              m_surface_impl;
    VkExtent2D                 m_extent          {0, 0};
    VkFormat                   m_color_format    {VK_FORMAT_UNDEFINED};
    VkFormat                   m_depth_format    {VK_FORMAT_UNDEFINED};
    uint32_t                   m_image_index     {0};
    std::vector<VkImage>       m_images;
    std::vector<VmaAllocation> m_allocations;
    std::vector<VkImageView>   m_views;
    std::vector<VkRenderPass>  m_render_pass;   // VkRenderPass each framebuffer was built against
    std::vector<VkFramebuffer> m_framebuffers;
    VkImage                    m_depth_image     {VK_NULL_HANDLE};
    VkImageView                m_depth_view      {VK_NULL_HANDLE};
    VmaAllocation              m_depth_allocation{VK_NULL_HANDLE};
};

} // namespace erhe::graphics
