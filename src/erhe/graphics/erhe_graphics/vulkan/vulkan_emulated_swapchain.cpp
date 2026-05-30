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
    // No-op: the emulated swapchain has no acquire/present state machine to
    // advance (present is a no-op).
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

} // namespace erhe::graphics
