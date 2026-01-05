#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"

#include <fmt/format.h>

#include <thread>

namespace erhe::graphics {

Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& descriptor)
    : m_device              {device}
    , m_device_impl         {device.get_impl()}
    , m_swapchain           {descriptor.swapchain}
    , m_color_attachments   {descriptor.color_attachments}
    , m_depth_attachment    {descriptor.depth_attachment}
    , m_stencil_attachment  {descriptor.stencil_attachment}
    , m_render_target_width {descriptor.render_target_width}
    , m_render_target_height{descriptor.render_target_height}
    , m_debug_label         {descriptor.debug_label}
    , m_debug_group_name    {fmt::format("Render pass: {}", descriptor.debug_label)}
{
    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

    // TODO non-swapchain render passes
    ERHE_VERIFY(m_swapchain != nullptr);

    {
        Surface_impl&                 surface_impl        = m_swapchain->get_impl().get_surface_impl();
        const Surface_create_info&    surface_create_info = surface_impl.get_surface_create_info();
        erhe::window::Context_window* context_window      = surface_create_info.context_window;
        ERHE_VERIFY(context_window != nullptr);

        const erhe::window::Window_configuration& window_configuration = context_window->get_window_configuration();

        // TODO multisample resolve
        const VkAttachmentDescription color_attachment_description{
            .flags          = 0,
            .format         = surface_impl.get_surface_format().format,
            .samples        = get_vulkan_sample_count(window_configuration.msaa_sample_count),
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = window_configuration.use_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR  : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = window_configuration.use_stencil ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        };

        const VkAttachmentReference color_attachment_reference{
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        m_clear_values.resize(1);
        m_clear_values.front() .color = VkClearColorValue{
            .float32 = {
                static_cast<float>(descriptor.color_attachments[0].clear_value[0]),
                static_cast<float>(descriptor.color_attachments[0].clear_value[1]),
                static_cast<float>(descriptor.color_attachments[0].clear_value[2]),
                static_cast<float>(descriptor.color_attachments[0].clear_value[3])
            }
        };

        // TODO Multisample resolve, stencil and depth
        ERHE_VERIFY(window_configuration.msaa_sample_count <= 1);
        ERHE_VERIFY(!window_configuration.use_stencil);
        ERHE_VERIFY(!window_configuration.use_depth);

        const VkSubpassDescription subpass_description{
            .flags                   = 0,                               // VkSubpassDescriptionFlag
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS, // VkPipelineBindPoint
            .inputAttachmentCount    = 0,                               // uint32_t
            .pInputAttachments       = nullptr,                         // const VkAttachmentReference*
            .colorAttachmentCount    = 1,                               // uint32_t
            .pColorAttachments       = &color_attachment_reference,     // const VkAttachmentReference*
            .pResolveAttachments     = nullptr,                         // const VkAttachmentReference*
            .pDepthStencilAttachment = nullptr,                         // const VkAttachmentReference*
            .preserveAttachmentCount = 0,                               // uint32_t
            .pPreserveAttachments    = nullptr                          // const uint32_t*
        };

        // Make the render pass wait for the VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT stage
        // - wait for the implicit subpass before or after the render pass
        // - wait for the swap chain to finish reading from the image
        // - wait before doing writes to color attachment
        const VkSubpassDependency subpass_dependency{
            .srcSubpass      = VK_SUBPASS_EXTERNAL,                           // uint32_t
            .dstSubpass      = 0,                                             // uint32_t
            .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags
            .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags
            .srcAccessMask   = 0,                                             // VkAccessFlags
            .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // VkAccessFlags
            .dependencyFlags = 0                                              // VkDependencyFlags
        };

        const VkRenderPassCreateInfo render_pass_create_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, // VkStructureType
            .pNext           = nullptr,                                   // const void*
            .flags           = 0,                                         // VkRenderPassCreateFlags
            .attachmentCount = 1,                                         // uint32_t
            .pAttachments    = &color_attachment_description,             // const VkAttachmentDescription*
            .subpassCount    = 1,                                         // uint32_t
            .pSubpasses      = &subpass_description,                      // const VkSubpassDescription*
            .dependencyCount = 1,                                         // uint32_t
            .pDependencies   = &subpass_dependency                        // const VkSubpassDependency*
        };

        log_render_pass->trace("Creating renderpass");
        result = vkCreateRenderPass(vulkan_device, &render_pass_create_info, nullptr, &m_render_pass);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkCreateRenderPass() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_RENDER_PASS,
            reinterpret_cast<uint64_t>(m_render_pass),
            "Render_pass_impl vulkan render_pass for surface/swapchain"
        );
    }
}


Render_pass_impl::~Render_pass_impl() noexcept
{
    if (m_render_pass != VK_NULL_HANDLE) {
        const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
        VkResult result = VK_SUCCESS;

        // TODO Queue thread safety
	    result = vkDeviceWaitIdle(vulkan_device);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkDeviceWaitIdle() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        vkDestroyRenderPass(vulkan_device, m_render_pass, nullptr);
    }
}

auto Render_pass_impl::get_swapchain() const -> Swapchain*
{
    return m_swapchain;
}

void Render_pass_impl::reset()
{
}

void Render_pass_impl::create()
{
}

auto Render_pass_impl::get_sample_count() const -> unsigned int
{
    ERHE_FATAL("Not implemented");
    return 0;
}

auto Render_pass_impl::check_status() const -> bool
{
    ERHE_FATAL("Not implemented");
    return false;
}

auto Render_pass_impl::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass_impl::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass_impl::get_debug_label() const -> const std::string&
{
    return m_debug_label;
}

auto Render_pass_impl::start_render_pass() -> VkCommandBuffer
{
    if (m_swapchain != nullptr) {
        VkRenderPassBeginInfo render_pass_begin_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType
            .pNext           = nullptr,                                  // const void*
            .renderPass      = m_render_pass,                            // VkRenderPass
            .framebuffer     = VK_NULL_HANDLE,         // filled in by Swapchain_impl m_swapchain_objects.framebuffers[m_acquired_image_index],
            .renderArea      = VkRect2D{{0,0}, {0,0}}, // filled in by Swapchain_impl VkRect2D{{0,0}, m_swapchain_extent},
            .clearValueCount = static_cast<uint32_t>(m_clear_values.size()), // uint32_t
            .pClearValues    = m_clear_values.data()                         // const VkClearValue*
        };
        return m_swapchain->get_impl().begin_render_pass(render_pass_begin_info);
    } else {
        ERHE_FATAL("Render_pass_impl::start_render_pass(): non-swapchain render passes not implemented");
    }
}

void Render_pass_impl::end_render_pass()
{
    if (m_swapchain != nullptr) {
        const bool end_ok    = m_swapchain->get_impl().end_render_pass();
        ERHE_VERIFY(end_ok);

        const bool submit_ok = m_swapchain->get_impl().submit_command_buffer();
        ERHE_VERIFY(submit_ok);
    } else {
        ERHE_FATAL("Render_pass_impl::end_render_pass(): non-swapchain render passes not implemented");
    }
}

} // namespace erhe::graphics
