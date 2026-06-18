#include "erhe_graphics/vulkan/vulkan_emulated_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <array>

namespace erhe::graphics {

Emulated_swapchain_impl::Emulated_swapchain_impl(Device_impl& device_impl, Surface_impl& surface_impl)
    : m_device_impl {device_impl}
    , m_surface_impl{surface_impl}
{
}

Emulated_swapchain_impl::~Emulated_swapchain_impl() noexcept
{
    release_resources();
}

void Emulated_swapchain_impl::release_resources()
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    // Drain the GPU before destroying any image / framebuffer that an
    // in-flight frame might still reference.
    vkDeviceWaitIdle(vulkan_device);

    for (const VkFramebuffer framebuffer : m_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vulkan_device, framebuffer, nullptr);
        }
    }
    for (const VkImageView view : m_views) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan_device, view, nullptr);
        }
    }

    VmaAllocator& allocator = m_device_impl.get_allocator();
    for (size_t i = 0; i < m_images.size(); ++i) {
        if (m_images[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, m_images[i], m_allocations[i]);
        }
    }
    if (m_depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan_device, m_depth_view, nullptr);
        m_depth_view = VK_NULL_HANDLE;
    }
    if (m_depth_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_depth_image, m_depth_allocation);
        m_depth_image      = VK_NULL_HANDLE;
        m_depth_allocation = VK_NULL_HANDLE;
    }

    m_framebuffers.clear();
    m_render_pass .clear();
    m_views       .clear();
    m_images      .clear();
    m_allocations .clear();
}

void Emulated_swapchain_impl::create_images()
{
    const Surface_create_info&          surface_create_info = m_surface_impl.get_surface_create_info();
    erhe::window::Context_window* const context_window      = surface_create_info.context_window;
    ERHE_VERIFY(context_window != nullptr);

    m_extent = VkExtent2D{
        static_cast<uint32_t>(context_window->get_width()),
        static_cast<uint32_t>(context_window->get_height())
    };
    m_color_format = m_surface_impl.get_surface_format().format;

    const uint32_t image_count = m_surface_impl.get_image_count();

    m_images      .resize(image_count, VK_NULL_HANDLE);
    m_allocations .resize(image_count, VK_NULL_HANDLE);
    m_views       .resize(image_count, VK_NULL_HANDLE);
    m_render_pass .resize(image_count, VK_NULL_HANDLE);
    m_framebuffers.resize(image_count, VK_NULL_HANDLE);

    VmaAllocator&  allocator     = m_device_impl.get_allocator();
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    for (uint32_t index = 0; index < image_count; ++index) {
        const VkImageCreateInfo image_create_info{
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = nullptr,
            .flags                 = 0,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = m_color_format,
            .extent                = {m_extent.width, m_extent.height, 1},
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            // TRANSFER_SRC so a future readback / file export is a drop-in.
            .usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices   = nullptr,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
        };
        const VmaAllocationCreateInfo allocation_create_info{
            .flags          = 0,
            .usage          = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            .preferredFlags = 0,
            .memoryTypeBits = 0,
            .pool           = VK_NULL_HANDLE,
            .pUserData      = nullptr,
            .priority       = 0.0f
        };
        VkResult result = vmaCreateImage(allocator, &image_create_info, &allocation_create_info, &m_images[index], &m_allocations[index], nullptr);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vmaCreateImage() for emulated swapchain failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
        vmaSetAllocationName(allocator, m_allocations[index], "Emulated swapchain image");
        m_device_impl.set_debug_label(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_images[index]), fmt::format("Emulated swapchain image {}", index).c_str());

        const VkImageViewCreateInfo view_create_info{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .image            = m_images[index],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = m_color_format,
            .components       = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A},
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        result = vkCreateImageView(vulkan_device, &view_create_info, nullptr, &m_views[index]);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkCreateImageView() for emulated swapchain failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
        m_device_impl.set_debug_label(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_views[index]), fmt::format("Emulated swapchain image view {}", index).c_str());
    }

    // Optional depth, matching the window configuration (mirrors Swapchain_impl).
    if (context_window->get_window_configuration().use_depth) {
        m_depth_format = to_vulkan(m_device_impl.choose_depth_stencil_format(0, 1));
        create_depth_image(m_extent.width, m_extent.height);
    }

    log_swapchain->info(
        "Emulated swapchain: {} image(s) {} x {} format {}{}",
        image_count, m_extent.width, m_extent.height, c_str(m_color_format),
        (m_depth_image != VK_NULL_HANDLE) ? fmt::format(" + depth {}", c_str(m_depth_format)) : std::string{}
    );
}

void Emulated_swapchain_impl::create_depth_image(const uint32_t width, const uint32_t height)
{
    const VkImageCreateInfo image_create_info{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = m_depth_format,
        .extent                = {width, height, 1},
        .mipLevels             = 1,
        .arrayLayers           = 1,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
    };
    const VmaAllocationCreateInfo allocation_create_info{
        .flags          = 0,
        .usage          = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .preferredFlags = 0,
        .memoryTypeBits = 0,
        .pool           = VK_NULL_HANDLE,
        .pUserData      = nullptr,
        .priority       = 0.0f
    };

    VmaAllocator& allocator = m_device_impl.get_allocator();
    VkResult result = vmaCreateImage(allocator, &image_create_info, &allocation_create_info, &m_depth_image, &m_depth_allocation, nullptr);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vmaCreateImage() for emulated swapchain depth failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    vmaSetAllocationName(allocator, m_depth_allocation, "Emulated swapchain depth image");
    m_device_impl.set_debug_label(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_depth_image), "Emulated swapchain depth image");

    const VkImageViewCreateInfo view_create_info{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = m_depth_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = m_depth_format,
        .components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
    };
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    result = vkCreateImageView(vulkan_device, &view_create_info, nullptr, &m_depth_view);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateImageView() for emulated swapchain depth failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    m_device_impl.set_debug_label(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_depth_view), "Emulated swapchain depth image view");
}

auto Emulated_swapchain_impl::wait_frame(Frame_state& out_frame_state) -> bool
{
    const size_t slot = static_cast<size_t>(m_device_impl.get_frame_in_flight_index());

    // Prime the device frame slot the same way the real swapchain's
    // setup_frame does, minus the legacy per-slot submit_fence: the device
    // timeline semaphore (already waited in Device_impl::wait_frame before
    // this call) is the pacing / recycle gate, so the device-frame command
    // pool can be reset without waiting a fence that no present would signal.
    m_device_impl.ensure_device_frame_command_buffer(slot);
    m_device_impl.reset_device_frame_command_pool(slot);

    if (m_images.empty()) {
        create_images();
    }

    out_frame_state = Frame_state{};
    out_frame_state.should_render = !m_images.empty();
    return out_frame_state.should_render;
}

auto Emulated_swapchain_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    // The headless dummy window never resizes (no native window, no events),
    // so request_resize is ignored.
    static_cast<void>(frame_begin_info);
    if (m_images.empty()) {
        return false;
    }
    m_image_index = (m_image_index + 1) % static_cast<uint32_t>(m_images.size());
    return true;
}

auto Emulated_swapchain_impl::present() -> bool
{
    // Headless: nothing to present. The frame's color image was left in
    // TRANSFER_SRC_OPTIMAL by the swapchain render pass for a future readback
    // / export. Never reached via the device implicit-present path (the
    // emulated frame does not register itself in swapchains_to_present).
    return true;
}

auto Emulated_swapchain_impl::acquire_swapchain_framebuffer(VkRenderPass render_pass) -> VkFramebuffer
{
    ERHE_VERIFY(render_pass != VK_NULL_HANDLE);
    const uint32_t index = m_image_index;

    if ((m_framebuffers[index] != VK_NULL_HANDLE) && (m_render_pass[index] == render_pass)) {
        return m_framebuffers[index];
    }

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    if (m_framebuffers[index] != VK_NULL_HANDLE) {
        // The window/swapchain render pass is stable in headless, so this
        // branch is not expected. If it ever fires, drain the GPU before
        // destroying the framebuffer an in-flight frame might still use.
        vkDeviceWaitIdle(vulkan_device);
        vkDestroyFramebuffer(vulkan_device, m_framebuffers[index], nullptr);
        m_framebuffers[index] = VK_NULL_HANDLE;
    }

    const std::array<VkImageView, 2> attachments = {m_views[index], m_depth_view};
    const uint32_t attachment_count = (m_depth_view != VK_NULL_HANDLE) ? 2u : 1u;
    const VkFramebufferCreateInfo framebuffer_create_info{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .renderPass      = render_pass,
        .attachmentCount = attachment_count,
        .pAttachments    = attachments.data(),
        .width           = m_extent.width,
        .height          = m_extent.height,
        .layers          = 1
    };
    const VkResult result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &m_framebuffers[index]);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateFramebuffer() for emulated swapchain failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    m_render_pass[index] = render_pass;
    m_device_impl.set_debug_label(VK_OBJECT_TYPE_FRAMEBUFFER, reinterpret_cast<uint64_t>(m_framebuffers[index]), fmt::format("Emulated swapchain framebuffer {}", index).c_str());
    return m_framebuffers[index];
}

auto Emulated_swapchain_impl::get_swapchain_extent() const -> VkExtent2D
{
    return m_extent;
}

void Emulated_swapchain_impl::mark_render_pass_recorded()
{
    // The emulated swapchain has no acquire/present state machine to advance
    // (present is a no-op). Record which image just had its swapchain render
    // pass recorded; that image holds the fully composited frame (viewports +
    // ImGui) in TRANSFER_SRC_OPTIMAL, and is what read_back_last_frame() reads.
    m_last_composited_index = m_image_index;
    m_have_composited       = true;
}

auto Emulated_swapchain_impl::get_surface_impl() -> Surface_impl&
{
    return m_surface_impl;
}

auto Emulated_swapchain_impl::has_depth() const -> bool
{
    return m_depth_image != VK_NULL_HANDLE;
}

auto Emulated_swapchain_impl::has_stencil() const -> bool
{
    return false;
}

auto Emulated_swapchain_impl::get_color_format() const -> erhe::dataformat::Format
{
    return to_erhe(m_color_format);
}

auto Emulated_swapchain_impl::get_depth_format() const -> erhe::dataformat::Format
{
    if (m_depth_format != VK_FORMAT_UNDEFINED) {
        return to_erhe(m_depth_format);
    }
    return erhe::dataformat::Format::format_undefined;
}

auto Emulated_swapchain_impl::get_depth_image_view() const -> VkImageView
{
    return m_depth_view;
}

auto Emulated_swapchain_impl::get_vk_depth_format() const -> VkFormat
{
    return m_depth_format;
}

auto Emulated_swapchain_impl::read_back_last_frame(
    uint32_t&               out_width,
    uint32_t&               out_height,
    std::vector<std::byte>& out_rgba8
) -> bool
{
    if (!m_have_composited || m_images.empty()) {
        return false;
    }

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    const VkQueue  queue         = m_device_impl.get_graphics_queue();
    const uint32_t queue_family  = m_device_impl.get_graphics_queue_family_index();
    VmaAllocator&  allocator     = m_device_impl.get_allocator();
    const uint32_t index         = m_last_composited_index;
    const VkImage  image         = m_images[index];
    const uint32_t width         = m_extent.width;
    const uint32_t height        = m_extent.height;
    if ((image == VK_NULL_HANDLE) || (width == 0) || (height == 0)) {
        return false;
    }

    // Drain the GPU so the last composited frame is complete and its color
    // image is settled in TRANSFER_SRC_OPTIMAL before the copy. This is a
    // diagnostic / infrequent path, so a full device wait is acceptable.
    vkDeviceWaitIdle(vulkan_device);

    const VkDeviceSize pixel_count = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height);
    const VkDeviceSize buffer_size = pixel_count * 4u; // source is 4 bytes/pixel (RGBA8 / BGRA8)

    // Host-visible, persistently mapped staging buffer.
    VkBuffer          staging_buffer     = VK_NULL_HANDLE;
    VmaAllocation     staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo staging_info{};
    const VkBufferCreateInfo buffer_create_info{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = buffer_size,
        .usage                 = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };
    const VmaAllocationCreateInfo alloc_create_info{
        .flags          = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage          = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        .memoryTypeBits = 0,
        .pool           = VK_NULL_HANDLE,
        .pUserData      = nullptr,
        .priority       = 0.0f
    };
    if (vmaCreateBuffer(allocator, &buffer_create_info, &alloc_create_info, &staging_buffer, &staging_allocation, &staging_info) != VK_SUCCESS) {
        log_swapchain->error("read_back_last_frame: staging buffer allocation failed");
        return false;
    }

    // One-shot command pool + primary command buffer on the graphics queue.
    VkCommandPool   command_pool   = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence         fence          = VK_NULL_HANDLE;
    const VkCommandPoolCreateInfo pool_create_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queue_family
    };
    bool ok = (vkCreateCommandPool(vulkan_device, &pool_create_info, nullptr, &command_pool) == VK_SUCCESS);

    if (ok) {
        const VkCommandBufferAllocateInfo cb_alloc_info{
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext              = nullptr,
            .commandPool        = command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        ok = (vkAllocateCommandBuffers(vulkan_device, &cb_alloc_info, &command_buffer) == VK_SUCCESS);
    }

    if (ok) {
        const VkCommandBufferBeginInfo begin_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(command_buffer, &begin_info);

        // The swapchain render pass left the image in TRANSFER_SRC_OPTIMAL.
        // Barrier with old == new layout to make the color writes available to
        // the transfer read.
        const VkImageMemoryBarrier barrier{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        vkCmdPipelineBarrier(
            command_buffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier
        );

        const VkBufferImageCopy region{
            .bufferOffset      = 0,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset       = {0, 0, 0},
            .imageExtent       = {width, height, 1}
        };
        vkCmdCopyImageToBuffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer, 1, &region);
        vkEndCommandBuffer(command_buffer);

        const VkFenceCreateInfo fence_create_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };
        ok = (vkCreateFence(vulkan_device, &fence_create_info, nullptr, &fence) == VK_SUCCESS);
    }

    if (ok) {
        const VkSubmitInfo submit_info{
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = nullptr,
            .waitSemaphoreCount   = 0,
            .pWaitSemaphores      = nullptr,
            .pWaitDstStageMask    = nullptr,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &command_buffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores    = nullptr
        };
        ok = (vkQueueSubmit(queue, 1, &submit_info, fence) == VK_SUCCESS);
        if (ok) {
            vkWaitForFences(vulkan_device, 1, &fence, VK_TRUE, UINT64_MAX);
        }
    }

    if (ok && (staging_info.pMappedData != nullptr)) {
        // Make the GPU writes visible to the host (no-op for coherent memory).
        vmaInvalidateAllocation(allocator, staging_allocation, 0, VK_WHOLE_SIZE);

        // Convert to tightly packed RGBA8: swap B/R for a BGRA source and force
        // an opaque alpha (the composited frame's alpha is not meaningful).
        const bool             is_bgra = (m_color_format == VK_FORMAT_B8G8R8A8_SRGB) || (m_color_format == VK_FORMAT_B8G8R8A8_UNORM);
        const std::byte* const src     = static_cast<const std::byte*>(staging_info.pMappedData);
        out_rgba8.resize(static_cast<std::size_t>(buffer_size));
        for (VkDeviceSize i = 0; i < pixel_count; ++i) {
            const std::size_t o  = static_cast<std::size_t>(i) * 4u;
            const std::byte   c0 = src[o + 0];
            const std::byte   c1 = src[o + 1];
            const std::byte   c2 = src[o + 2];
            out_rgba8[o + 0] = is_bgra ? c2 : c0; // R
            out_rgba8[o + 1] = c1;                // G
            out_rgba8[o + 2] = is_bgra ? c0 : c2; // B
            out_rgba8[o + 3] = std::byte{0xff};   // A
        }
        out_width  = width;
        out_height = height;
    } else {
        ok = false;
    }

    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(vulkan_device, fence, nullptr);
    }
    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vulkan_device, command_pool, nullptr);
    }
    vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);

    return ok;
}

} // namespace erhe::graphics
