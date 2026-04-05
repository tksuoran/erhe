#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"

#include <fmt/format.h>

#include <thread>

namespace erhe::graphics {

namespace {

auto to_vk_load_op(Load_action action) -> VkAttachmentLoadOp
{
    switch (action) {
        case Load_action::Clear:     return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case Load_action::Load:      return VK_ATTACHMENT_LOAD_OP_LOAD;
        case Load_action::Dont_care: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default:                     return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

auto to_vk_store_op(Store_action action) -> VkAttachmentStoreOp
{
    switch (action) {
        case Store_action::Store:                          return VK_ATTACHMENT_STORE_OP_STORE;
        case Store_action::Dont_care:                      return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case Store_action::Multisample_resolve:            return VK_ATTACHMENT_STORE_OP_STORE;
        case Store_action::Store_and_multisample_resolve:  return VK_ATTACHMENT_STORE_OP_STORE;
        default:                                           return VK_ATTACHMENT_STORE_OP_STORE;
    }
}

} // anonymous namespace

Render_pass_impl* Render_pass_impl::s_active_render_pass{nullptr};

auto Render_pass_impl::get_active_command_buffer() -> VkCommandBuffer
{
    if (s_active_render_pass != nullptr) {
        return s_active_render_pass->m_command_buffer;
    }
    return VK_NULL_HANDLE;
}

auto Render_pass_impl::get_active_render_pass() -> VkRenderPass
{
    if (s_active_render_pass != nullptr) {
        return s_active_render_pass->m_render_pass;
    }
    return VK_NULL_HANDLE;
}

auto Render_pass_impl::get_active_render_pass_impl() -> Render_pass_impl*
{
    return s_active_render_pass;
}

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
    , m_debug_group_name    {fmt::format("Render pass: {}", descriptor.debug_label.string_view())}
{
    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

    if (m_swapchain != nullptr) {
        // Swapchain render pass
        Surface_impl&                 surface_impl       = m_swapchain->get_impl().get_surface_impl();
        const Surface_create_info&    surface_create_info = surface_impl.get_surface_create_info();
        erhe::window::Context_window* context_window      = surface_create_info.context_window;
        ERHE_VERIFY(context_window != nullptr);

        const erhe::window::Window_configuration& window_configuration = context_window->get_window_configuration();

        Swapchain_impl& swapchain_impl = m_swapchain->get_impl();
        const VkSampleCountFlagBits sample_count = get_vulkan_sample_count(window_configuration.msaa_sample_count);

        std::vector<VkAttachmentDescription> attachment_descriptions;
        attachment_descriptions.push_back(VkAttachmentDescription{
            .flags          = 0,
            .format         = surface_impl.get_surface_format().format,
            .samples        = sample_count,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        });

        const VkAttachmentReference color_attachment_reference{
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        m_clear_values.push_back(VkClearValue{
            .color = VkClearColorValue{
                .float32 = {
                    static_cast<float>(descriptor.color_attachments[0].clear_value[0]),
                    static_cast<float>(descriptor.color_attachments[0].clear_value[1]),
                    static_cast<float>(descriptor.color_attachments[0].clear_value[2]),
                    static_cast<float>(descriptor.color_attachments[0].clear_value[3])
                }
            }
        });

        // Depth attachment -- only if swapchain has a depth image
        const bool has_depth = swapchain_impl.get_depth_image_view() != VK_NULL_HANDLE;
        VkAttachmentReference depth_attachment_reference{};
        if (has_depth) {
            attachment_descriptions.push_back(VkAttachmentDescription{
                .flags          = 0,
                .format         = swapchain_impl.get_depth_format(),
                .samples        = sample_count,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            });
            depth_attachment_reference = VkAttachmentReference{
                .attachment = 1,
                .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            };
            VkClearValue depth_clear{};
            depth_clear.depthStencil = VkClearDepthStencilValue{
                .depth   = static_cast<float>(m_depth_attachment.clear_value[0]),
                .stencil = 0
            };
            m_clear_values.push_back(depth_clear);
        }

        const VkSubpassDescription subpass_description{
            .flags                   = 0,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount    = 0,
            .pInputAttachments       = nullptr,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &color_attachment_reference,
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = has_depth ? &depth_attachment_reference : nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments    = nullptr
        };

        const VkSubpassDependency subpass_dependency{
            .srcSubpass      = VK_SUBPASS_EXTERNAL,
            .dstSubpass      = 0,
            .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask   = 0,
            .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0
        };

        const VkRenderPassCreateInfo render_pass_create_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .attachmentCount = static_cast<uint32_t>(attachment_descriptions.size()),
            .pAttachments    = attachment_descriptions.data(),
            .subpassCount    = 1,
            .pSubpasses      = &subpass_description,
            .dependencyCount = 1,
            .pDependencies   = &subpass_dependency
        };

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
    } else {
        // Non-swapchain (off-screen) render pass
        std::vector<VkAttachmentDescription> attachment_descriptions;
        std::vector<VkAttachmentReference>   color_attachment_references;
        std::vector<VkImageView>             image_views;
        VkAttachmentReference                depth_stencil_reference{};
        bool                                 has_depth_stencil = false;
        uint32_t                             attachment_index  = 0;

        // Color attachments
        for (std::size_t i = 0; i < m_color_attachments.size(); ++i) {
            const Render_pass_attachment_descriptor& att = m_color_attachments[i];
            if (!att.is_defined()) {
                continue;
            }

            VkFormat vk_format = to_vulkan(att.texture->get_pixelformat());

            attachment_descriptions.push_back(VkAttachmentDescription{
                .flags          = 0,
                .format         = vk_format,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = to_vk_load_op(att.load_action),
                .storeOp        = to_vk_store_op(att.store_action),
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = (att.load_action == Load_action::Load) ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });

            color_attachment_references.push_back(VkAttachmentReference{
                .attachment = attachment_index,
                .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            });

            VkImageView view = att.texture->get_impl().get_vk_image_view();
            image_views.push_back(view);

            // Clear values
            VkClearValue clear_value{};
            clear_value.color = VkClearColorValue{
                .float32 = {
                    static_cast<float>(att.clear_value[0]),
                    static_cast<float>(att.clear_value[1]),
                    static_cast<float>(att.clear_value[2]),
                    static_cast<float>(att.clear_value[3])
                }
            };
            m_clear_values.push_back(clear_value);

            ++attachment_index;
        }

        // Depth attachment
        if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
            VkFormat depth_format = to_vulkan(m_depth_attachment.texture->get_pixelformat());

            attachment_descriptions.push_back(VkAttachmentDescription{
                .flags          = 0,
                .format         = depth_format,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = to_vk_load_op(m_depth_attachment.load_action),
                .storeOp        = to_vk_store_op(m_depth_attachment.store_action),
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = (m_depth_attachment.load_action == Load_action::Load) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            });

            depth_stencil_reference = VkAttachmentReference{
                .attachment = attachment_index,
                .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            };
            has_depth_stencil = true;

            VkImageView view = m_depth_attachment.texture->get_impl().get_vk_image_view();
            image_views.push_back(view);

            VkClearValue clear_value{};
            clear_value.depthStencil = VkClearDepthStencilValue{
                .depth   = static_cast<float>(m_depth_attachment.clear_value[0]),
                .stencil = 0
            };
            m_clear_values.push_back(clear_value);

            ++attachment_index;
        }

        const VkSubpassDescription subpass_description{
            .flags                   = 0,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount    = 0,
            .pInputAttachments       = nullptr,
            .colorAttachmentCount    = static_cast<uint32_t>(color_attachment_references.size()),
            .pColorAttachments       = color_attachment_references.empty() ? nullptr : color_attachment_references.data(),
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = has_depth_stencil ? &depth_stencil_reference : nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments    = nullptr
        };

        // Dependencies for layout transitions
        std::vector<VkSubpassDependency> dependencies;
        dependencies.push_back(VkSubpassDependency{
            .srcSubpass      = VK_SUBPASS_EXTERNAL,
            .dstSubpass      = 0,
            .srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask   = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0
        });
        dependencies.push_back(VkSubpassDependency{
            .srcSubpass      = 0,
            .dstSubpass      = VK_SUBPASS_EXTERNAL,
            .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask   = VK_ACCESS_SHADER_READ_BIT,
            .dependencyFlags = 0
        });

        const VkRenderPassCreateInfo render_pass_create_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .attachmentCount = static_cast<uint32_t>(attachment_descriptions.size()),
            .pAttachments    = attachment_descriptions.empty() ? nullptr : attachment_descriptions.data(),
            .subpassCount    = 1,
            .pSubpasses      = &subpass_description,
            .dependencyCount = static_cast<uint32_t>(dependencies.size()),
            .pDependencies   = dependencies.data()
        };

        result = vkCreateRenderPass(vulkan_device, &render_pass_create_info, nullptr, &m_render_pass);
        if (result != VK_SUCCESS) {
            log_render_pass->critical("vkCreateRenderPass() failed for off-screen pass with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Create framebuffer
        const VkFramebufferCreateInfo framebuffer_create_info{
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .renderPass      = m_render_pass,
            .attachmentCount = static_cast<uint32_t>(image_views.size()),
            .pAttachments    = image_views.empty() ? nullptr : image_views.data(),
            .width           = static_cast<uint32_t>(m_render_target_width),
            .height          = static_cast<uint32_t>(m_render_target_height),
            .layers          = 1
        };

        result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &m_framebuffer);
        if (result != VK_SUCCESS) {
            log_render_pass->critical("vkCreateFramebuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Create a command pool for this render pass
        const VkCommandPoolCreateInfo command_pool_create_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_device_impl.get_graphics_queue_family_index()
        };
        result = vkCreateCommandPool(vulkan_device, &command_pool_create_info, nullptr, &m_command_pool);
        if (result != VK_SUCCESS) {
            log_render_pass->critical("vkCreateCommandPool() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_RENDER_PASS,
            reinterpret_cast<uint64_t>(m_render_pass),
            fmt::format("Render_pass_impl off-screen: {}", descriptor.debug_label.string_view())
        );

        log_render_pass->info(
            "Off-screen render pass created: {}x{}, {} color attachments, depth={}",
            m_render_target_width, m_render_target_height,
            color_attachment_references.size(),
            has_depth_stencil ? "yes" : "no"
        );
    }
}


Render_pass_impl::~Render_pass_impl() noexcept
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    vkDeviceWaitIdle(vulkan_device);

    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vulkan_device, m_framebuffer, nullptr);
    }
    if (m_command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vulkan_device, m_command_pool, nullptr);
    }
    if (m_render_pass != VK_NULL_HANDLE) {
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
    // TODO Read sample count from attachment textures or swapchain
    return 1;
}

auto Render_pass_impl::check_status() const -> bool
{
    return m_render_pass != VK_NULL_HANDLE;
}

auto Render_pass_impl::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass_impl::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Render_pass_impl::start_render_pass()
{
    s_active_render_pass = this;

    if (m_swapchain != nullptr) {
        // Swapchain render pass
        VkRenderPassBeginInfo render_pass_begin_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = m_render_pass,
            .framebuffer     = VK_NULL_HANDLE,         // filled in by Swapchain_impl
            .renderArea      = VkRect2D{{0,0}, {0,0}}, // filled in by Swapchain_impl
            .clearValueCount = static_cast<uint32_t>(m_clear_values.size()),
            .pClearValues    = m_clear_values.data()
        };
        m_command_buffer = m_swapchain->get_impl().begin_render_pass(render_pass_begin_info);
    } else {
        // Non-swapchain render pass
        VkDevice vulkan_device = m_device_impl.get_vulkan_device();

        // Allocate command buffer from our command pool
        const VkCommandBufferAllocateInfo allocate_info{
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext              = nullptr,
            .commandPool        = m_command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VkResult result = vkAllocateCommandBuffers(vulkan_device, &allocate_info, &m_command_buffer);
        if (result != VK_SUCCESS) {
            log_render_pass->error("vkAllocateCommandBuffers() failed with {}", static_cast<int32_t>(result));
            s_active_render_pass = nullptr;
            return;
        }

        // Begin command buffer
        const VkCommandBufferBeginInfo begin_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        result = vkBeginCommandBuffer(m_command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            log_render_pass->error("vkBeginCommandBuffer() failed with {}", static_cast<int32_t>(result));
            s_active_render_pass = nullptr;
            return;
        }

        // Begin render pass
        const VkRenderPassBeginInfo render_pass_begin_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = m_render_pass,
            .framebuffer     = m_framebuffer,
            .renderArea      = VkRect2D{
                .offset = {0, 0},
                .extent = {
                    static_cast<uint32_t>(m_render_target_width),
                    static_cast<uint32_t>(m_render_target_height)
                }
            },
            .clearValueCount = static_cast<uint32_t>(m_clear_values.size()),
            .pClearValues    = m_clear_values.data()
        };
        vkCmdBeginRenderPass(m_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
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
        // End the render pass and submit the command buffer
        if (m_command_buffer != VK_NULL_HANDLE) {
            vkCmdEndRenderPass(m_command_buffer);

            VkResult result = vkEndCommandBuffer(m_command_buffer);
            if (result != VK_SUCCESS) {
                log_render_pass->error("vkEndCommandBuffer() failed with {}", static_cast<int32_t>(result));
            } else {
                // Submit
                VkQueue graphics_queue = m_device_impl.get_graphics_queue();
                const VkSubmitInfo submit_info{
                    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext                = nullptr,
                    .waitSemaphoreCount   = 0,
                    .pWaitSemaphores      = nullptr,
                    .pWaitDstStageMask    = nullptr,
                    .commandBufferCount   = 1,
                    .pCommandBuffers      = &m_command_buffer,
                    .signalSemaphoreCount = 0,
                    .pSignalSemaphores    = nullptr
                };
                result = vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
                if (result != VK_SUCCESS) {
                    log_render_pass->error("vkQueueSubmit() failed with {}", static_cast<int32_t>(result));
                }

                // Wait for completion (simple but correct; could use fences for pipelining)
                vkQueueWaitIdle(graphics_queue);
            }

            // Reset command pool for reuse
            vkResetCommandPool(m_device_impl.get_vulkan_device(), m_command_pool, 0);
        }
    }

    m_command_buffer = VK_NULL_HANDLE;
    s_active_render_pass = nullptr;
}

} // namespace erhe::graphics
