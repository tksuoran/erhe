#include "erhe_graphics/vulkan/vulkan_blit_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_immediate_commands.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_submit_handle.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

namespace erhe::graphics {

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device)
    : m_device{device}
{
}

Blit_command_encoder_impl::~Blit_command_encoder_impl() noexcept
{
}

void Blit_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    // Blit encoder does not use buffer bindings
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Blit_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    // Blit encoder does not use buffer bindings
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

void Blit_command_encoder_impl::blit_framebuffer(
    const Render_pass& source_renderpass,
    glm::ivec2         source_origin,
    glm::ivec2         source_size,
    const Render_pass& destination_renderpass,
    glm::ivec2         destination_origin
)
{
    // For Vulkan, blit between render pass color attachments.
    // Both render passes must have a color attachment with a texture that has an image.
    const Render_pass_impl& src_impl = source_renderpass.get_impl();
    const Render_pass_impl& dst_impl = destination_renderpass.get_impl();

    // For swapchain render passes, we cannot directly access the swapchain image here.
    // This operation is primarily used for XR framebuffer mirroring.
    if ((source_renderpass.get_swapchain() != nullptr) || (destination_renderpass.get_swapchain() != nullptr)) {
        log_texture->warn("blit_framebuffer: swapchain render pass blit not yet supported for Vulkan");
        return;
    }

    log_texture->warn("blit_framebuffer: not yet fully implemented for Vulkan (non-swapchain)");
    static_cast<void>(source_origin);
    static_cast<void>(source_size);
    static_cast<void>(destination_origin);
    static_cast<void>(src_impl);
    static_cast<void>(dst_impl);
}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    std::uintptr_t source_slice,
    std::uintptr_t source_level,
    glm::ivec3     source_origin,
    glm::ivec3     source_size,
    const Texture* destination_texture,
    std::uintptr_t destination_slice,
    std::uintptr_t destination_level,
    glm::ivec3     destination_origin
)
{
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }

    VkImage src_image = source_texture->get_impl().get_vk_image();
    VkImage dst_image = destination_texture->get_impl().get_vk_image();

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_device.get_impl().get_immediate_commands().acquire();

    // Transition source to transfer src
    const VkImageMemoryBarrier src_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    // Transition destination to transfer dst
    const VkImageMemoryBarrier dst_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = dst_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(destination_level), 1, static_cast<uint32_t>(destination_slice), 1}
    };
    VkImageMemoryBarrier pre_barriers[] = {src_barrier, dst_barrier};
    vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre_barriers);

    const VkImageCopy region{
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), static_cast<uint32_t>(source_slice), 1},
        .srcOffset      = {source_origin.x, source_origin.y, source_origin.z},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(destination_level), static_cast<uint32_t>(destination_slice), 1},
        .dstOffset      = {destination_origin.x, destination_origin.y, destination_origin.z},
        .extent         = {static_cast<uint32_t>(source_size.x), static_cast<uint32_t>(source_size.y), static_cast<uint32_t>(source_size.z)}
    };
    vkCmdCopyImage(cmd.m_cmd_buf, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition both back to shader read
    const VkImageMemoryBarrier src_post{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    const VkImageMemoryBarrier dst_post{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = dst_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(destination_level), 1, static_cast<uint32_t>(destination_slice), 1}
    };
    VkImageMemoryBarrier post_barriers[] = {src_post, dst_post};
    vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, post_barriers);

    Submit_handle submit = m_device.get_impl().get_immediate_commands().submit(cmd);
    m_device.get_impl().get_immediate_commands().wait(submit);
}

// Buffer to texture copy
void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer*  source_buffer,
    std::uintptr_t source_offset,
    std::uintptr_t source_bytes_per_row,
    std::uintptr_t source_bytes_per_image,
    glm::ivec3     source_size,
    const Texture* destination_texture,
    std::uintptr_t destination_slice,
    std::uintptr_t destination_level,
    glm::ivec3     destination_origin
)
{
    const VkBuffer vk_source_buffer     = source_buffer->get_impl().get_vk_buffer();
    const VkImage  vk_destination_image = destination_texture->get_impl().get_vk_image();

    const VkBufferImageCopy region{
        .bufferOffset      = source_offset,
        .bufferRowLength   = static_cast<uint32_t>(source_bytes_per_row),
        .bufferImageHeight = static_cast<uint32_t>(source_bytes_per_image),
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = static_cast<uint32_t>(destination_level),
            .baseArrayLayer = static_cast<uint32_t>(destination_slice),
            .layerCount     = 1
        },
        .imageOffset = {
            .x = destination_origin.x,
            .y = destination_origin.y,
            .z = destination_origin.z
        },
        .imageExtent = {
            .width  = static_cast<uint32_t>(source_size.x),
            .height = static_cast<uint32_t>(source_size.y),
            .depth  = static_cast<uint32_t>(source_size.z)
        }
    };

    const Vulkan_immediate_commands::Command_buffer_wrapper& command_buffer_wrapper = m_device.get_impl().get_immediate_commands().acquire();

    // Transition image to transfer destination layout
    const VkImageMemoryBarrier pre_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = vk_destination_image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = static_cast<uint32_t>(destination_level),
            .levelCount     = 1,
            .baseArrayLayer = static_cast<uint32_t>(destination_slice),
            .layerCount     = 1
        }
    };
    vkCmdPipelineBarrier(
        command_buffer_wrapper.m_cmd_buf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &pre_barrier
    );

    vkCmdCopyBufferToImage(
        command_buffer_wrapper.m_cmd_buf,
        vk_source_buffer,
        vk_destination_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // Transition image to shader read layout
    const VkImageMemoryBarrier post_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = vk_destination_image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = static_cast<uint32_t>(destination_level),
            .levelCount     = 1,
            .baseArrayLayer = static_cast<uint32_t>(destination_slice),
            .layerCount     = 1
        }
    };
    vkCmdPipelineBarrier(
        command_buffer_wrapper.m_cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &post_barrier
    );
}

// Copy from texture to buffer
void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    std::uintptr_t source_slice,
    std::uintptr_t source_level,
    glm::ivec3     source_origin,
    glm::ivec3     source_size,
    const Buffer*  destination_buffer,
    std::uintptr_t destination_offset,
    std::uintptr_t destination_bytes_per_row,
    std::uintptr_t destination_bytes_per_image
)
{
    if ((source_texture == nullptr) || (destination_buffer == nullptr)) {
        return;
    }

    VkImage  src_image  = source_texture->get_impl().get_vk_image();
    VkBuffer dst_buffer = destination_buffer->get_impl().get_vk_buffer();

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_device.get_impl().get_immediate_commands().acquire();

    // Transition source to transfer src
    const VkImageMemoryBarrier pre_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &pre_barrier);

    const VkBufferImageCopy region{
        .bufferOffset      = static_cast<VkDeviceSize>(destination_offset),
        .bufferRowLength   = static_cast<uint32_t>(destination_bytes_per_row),
        .bufferImageHeight = static_cast<uint32_t>(destination_bytes_per_image),
        .imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), static_cast<uint32_t>(source_slice), 1},
        .imageOffset       = {source_origin.x, source_origin.y, source_origin.z},
        .imageExtent       = {static_cast<uint32_t>(source_size.x), static_cast<uint32_t>(source_size.y), static_cast<uint32_t>(source_size.z)}
    };
    vkCmdCopyImageToBuffer(cmd.m_cmd_buf, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_buffer, 1, &region);

    // Transition source back to shader read
    const VkImageMemoryBarrier post_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &post_barrier);

    Submit_handle submit = m_device.get_impl().get_immediate_commands().submit(cmd);
    m_device.get_impl().get_immediate_commands().wait(submit);
}

void Blit_command_encoder_impl::generate_mipmaps(const Texture* texture)
{
    if (texture == nullptr) {
        return;
    }

    VkImage image       = texture->get_impl().get_vk_image();
    int     level_count = texture->get_level_count();
    int     width       = texture->get_width();
    int     height      = texture->get_height();

    if (level_count <= 1) {
        return;
    }

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_device.get_impl().get_immediate_commands().acquire();

    // Transition mip level 0 to transfer src (assume it was just written as color attachment or transfer dst)
    {
        const VkImageMemoryBarrier barrier{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    int mip_width  = width;
    int mip_height = height;

    for (int i = 1; i < level_count; ++i) {
        int next_width  = (mip_width  > 1) ? (mip_width  / 2) : 1;
        int next_height = (mip_height > 1) ? (mip_height / 2) : 1;

        // Transition mip level i to transfer dst
        {
            const VkImageMemoryBarrier barrier{
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext               = nullptr,
                .srcAccessMask       = 0,
                .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = image,
                .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 1, 0, 1}
            };
            vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        // Blit from mip level i-1 to mip level i
        const VkImageBlit blit_region{
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i - 1), 0, 1},
            .srcOffsets     = {{0, 0, 0}, {mip_width, mip_height, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 0, 1},
            .dstOffsets     = {{0, 0, 0}, {next_width, next_height, 1}}
        };
        vkCmdBlitImage(
            cmd.m_cmd_buf,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit_region,
            VK_FILTER_LINEAR
        );

        // Transition mip level i from transfer dst to transfer src (for next iteration)
        {
            const VkImageMemoryBarrier barrier{
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext               = nullptr,
                .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = image,
                .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 1, 0, 1}
            };
            vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        mip_width  = next_width;
        mip_height = next_height;
    }

    // Transition all mip levels to shader read optimal
    {
        const VkImageMemoryBarrier barrier{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(level_count), 0, 1}
        };
        vkCmdPipelineBarrier(cmd.m_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    Submit_handle submit = m_device.get_impl().get_immediate_commands().submit(cmd);
    m_device.get_impl().get_immediate_commands().wait(submit);
}

void Blit_command_encoder_impl::fill_buffer(
    const Buffer*  buffer,
    std::uintptr_t offset,
    std::uintptr_t length,
    uint8_t        value
)
{
    if (buffer == nullptr) {
        return;
    }

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_device.get_impl().get_immediate_commands().acquire();

    // vkCmdFillBuffer fills with a uint32_t value, replicate the byte across 4 bytes
    uint32_t fill_value = static_cast<uint32_t>(value) |
                          (static_cast<uint32_t>(value) << 8) |
                          (static_cast<uint32_t>(value) << 16) |
                          (static_cast<uint32_t>(value) << 24);

    vkCmdFillBuffer(
        cmd.m_cmd_buf,
        buffer->get_impl().get_vk_buffer(),
        static_cast<VkDeviceSize>(offset),
        static_cast<VkDeviceSize>(length),
        fill_value
    );

    Submit_handle submit = m_device.get_impl().get_immediate_commands().submit(cmd);
    m_device.get_impl().get_immediate_commands().wait(submit);
}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    std::uintptr_t source_slice,
    std::uintptr_t source_level,
    const Texture* destination_texture,
    std::uintptr_t destination_slice,
    std::uintptr_t destination_level,
    std::uintptr_t slice_count,
    std::uintptr_t level_count
)
{
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }

    for (std::uintptr_t slice = 0; slice < slice_count; ++slice) {
        for (std::uintptr_t level = 0; level < level_count; ++level) {
            int width  = source_texture->get_width(static_cast<unsigned int>(source_level + level));
            int height = source_texture->get_height(static_cast<unsigned int>(source_level + level));
            if ((width <= 0) || (height <= 0)) {
                continue;
            }
            copy_from_texture(
                source_texture,
                source_slice + slice,
                source_level + level,
                glm::ivec3{0, 0, 0},
                glm::ivec3{width, height, 1},
                destination_texture,
                destination_slice + slice,
                destination_level + level,
                glm::ivec3{0, 0, 0}
            );
        }
    }
}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    const Texture* destination_texture
)
{
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }

    int level_count = source_texture->get_level_count();
    int layer_count = source_texture->get_array_layer_count();
    if (layer_count < 1) {
        layer_count = 1;
    }

    copy_from_texture(source_texture, 0, 0, destination_texture, 0, 0, static_cast<std::uintptr_t>(layer_count), static_cast<std::uintptr_t>(level_count));
}

void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer*  source_buffer,
    std::uintptr_t source_offset,
    const Buffer*  destination_buffer,
    std::uintptr_t destination_offset,
    std::uintptr_t size
)
{
    if ((source_buffer == nullptr) || (destination_buffer == nullptr)) {
        return;
    }

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_device.get_impl().get_immediate_commands().acquire();

    const VkBufferCopy region{
        .srcOffset = static_cast<VkDeviceSize>(source_offset),
        .dstOffset = static_cast<VkDeviceSize>(destination_offset),
        .size      = static_cast<VkDeviceSize>(size)
    };

    vkCmdCopyBuffer(
        cmd.m_cmd_buf,
        source_buffer->get_impl().get_vk_buffer(),
        destination_buffer->get_impl().get_vk_buffer(),
        1,
        &region
    );

    Submit_handle submit = m_device.get_impl().get_immediate_commands().submit(cmd);
    m_device.get_impl().get_immediate_commands().wait(submit);
}

} // namespace erhe::graphics
