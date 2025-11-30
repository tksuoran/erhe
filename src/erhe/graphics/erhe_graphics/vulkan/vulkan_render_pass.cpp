#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include <thread>

namespace erhe::graphics {

Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& descriptor)
    : m_device              {device}
    , m_swapchain           {descriptor.swapchain}
    , m_color_attachments   {descriptor.color_attachments}
    , m_depth_attachment    {descriptor.depth_attachment}
    , m_stencil_attachment  {descriptor.stencil_attachment}
    , m_render_target_width {descriptor.render_target_width}
    , m_render_target_height{descriptor.render_target_height}
    , m_debug_label         {descriptor.debug_label}
    , m_debug_group_name    {fmt::format("Render pass: {}", descriptor.debug_label)}
{
    Swapchain_impl* swapchain_impl = (m_swapchain != nullptr)
        ? &m_swapchain->get_impl()
        : nullptr;

    // Vulkan needs one renderpass that can be shared for all swapchain images,
    // and framebuffer per swapchain image
    const VkAttachmentDescription color_attachment_description{
        .flags          = 0,                                // VkAttachmentDescriptionFlags    flags;
        .format         = VK_FORMAT_R8G8B8A8_SRGB,          // VkFormat                        format;
        .samples        = VK_SAMPLE_COUNT_1_BIT,            // VkSampleCountFlagBits           samples;
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,      // VkAttachmentLoadOp              loadOp;
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,     // VkAttachmentStoreOp             storeOp;
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // VkAttachmentLoadOp              stencilLoadOp;
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, // VkAttachmentStoreOp             stencilStoreOp;
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,        // VkImageLayout                   initialLayout;
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR   // VkImageLayout                   finalLayout;
    };

    const VkAttachmentReference color_attachment_reference{
        .attachment = 0,                                       // uint32_t           attachment;
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // VkImageLayout      layout;
    };

    VkSubpassDescription subpass_description{
        .flags                   = 0,                               // VkSubpassDescriptionFlags       flags;
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS, // VkPipelineBindPoint             pipelineBindPoint;
        .inputAttachmentCount    = 0,                               // uint32_t                        inputAttachmentCount;
        .pInputAttachments       = 0,                               // const VkAttachmentReference*    pInputAttachments;
        .colorAttachmentCount    = 1,                               // uint32_t                        colorAttachmentCount;
        .pColorAttachments       = &color_attachment_reference,     // const VkAttachmentReference*    pColorAttachments;
        .pResolveAttachments     = 0,                               // const VkAttachmentReference*    pResolveAttachments;
        .pDepthStencilAttachment = 0,                               // const VkAttachmentReference*    pDepthStencilAttachment;
        .preserveAttachmentCount = 0,                               // uint32_t                        preserveAttachmentCount;
        .pPreserveAttachments    = 0,                               // const uint32_t*                 pPreserveAttachments;
    };

    VkRenderPassCreateInfo render_pass_create_info{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, // VkStructureType                   sType;
        .pNext           = nullptr,                                   // const void*                       pNext;
        .flags           = 0,                                         // VkRenderPassCreateFlags           flags;
        .attachmentCount = 1,                                         // uint32_t                          attachmentCount;
        .pAttachments    = &color_attachment_description,             // const VkAttachmentDescription*    pAttachments;
        .subpassCount    = 1,                                         // uint32_t                          subpassCount;
        .pSubpasses      = &subpass_description,                      // const VkSubpassDescription*       pSubpasses;
        .dependencyCount = 0, // uint32_t                          dependencyCount;
        .pDependencies   = 0, // const VkSubpassDependency*        pDependencies;
    };

    Device_impl& device_impl = device.get_impl();
    VkRenderPass renderpass{VK_NULL_HANDLE};
    VkResult result = device_impl.create_render_pass(&render_pass_create_info, nullptr, &renderpass);
    if (result != VK_SUCCESS) {
        ERHE_FATAL("vkCreateRenderPass failed");
    }

    const size_t image_count = (swapchain_impl != nullptr) ? swapchain_impl->get_image_count() : 1;

    for (size_t i = 0; i < image_count; ++i) {
        //VkImageView attachments[] = { m_swapchain_image_views[image_index] };

        // VkFramebufferCreateInfo framebufferInfo{};
        // framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        // framebufferInfo.renderPass      = renderPass;
        // framebufferInfo.attachmentCount = 1;
        // framebufferInfo.pAttachments    = attachments;
        // framebufferInfo.width           = swapChainExtent.width;
        // framebufferInfo.height          = swapChainExtent.height;
        // framebufferInfo.layers          = 1;
        // 
        // if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
        //     throw std::runtime_error("failed to create framebuffer!");
        // }
    }
}

Render_pass_impl::~Render_pass_impl() noexcept
{
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
    return 0;
}

auto Render_pass_impl::check_status() const -> bool
{
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

void Render_pass_impl::start_render_pass()
{
}

void Render_pass_impl::end_render_pass()
{
}

} // namespace erhe::graphics
