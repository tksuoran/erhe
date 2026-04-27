#include "erhe_graphics/vulkan/vulkan_blit_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#include <mutex>

namespace erhe::graphics {

namespace {

// Thin wrapper that pulls the encoder's VkCommandBuffer for each blit
// method. The caller must have called begin() on the cb before
// constructing the encoder; the cb is bound by Device_impl::get_command_buffer
// at allocation time and used directly here.
class Recording_scope final
{
public:
    Recording_scope(Device_impl& d, Command_buffer& command_buffer) : device{d}
    {
        cb = command_buffer.get_impl().get_vulkan_command_buffer();
        ERHE_VERIFY(cb != VK_NULL_HANDLE);
    }
    Recording_scope(const Recording_scope&)            = delete;
    Recording_scope& operator=(const Recording_scope&) = delete;
    Recording_scope(Recording_scope&&)                 = delete;
    Recording_scope& operator=(Recording_scope&&)      = delete;

    Device_impl&    device;
    VkCommandBuffer cb{VK_NULL_HANDLE};
};

} // namespace

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : m_device        {device}
    , m_command_buffer{command_buffer}
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

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    // Transition source to transfer src
    const VkImageMemoryBarrier2 src_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    // Transition destination to transfer dst
    const VkImageMemoryBarrier2 dst_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask       = 0,
        .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = dst_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(destination_level), 1, static_cast<uint32_t>(destination_slice), 1}
    };
    const VkImageMemoryBarrier2 pre_barriers[] = {src_barrier, dst_barrier};
    cmd_pipeline_image_barriers2(scope.cb, 2, pre_barriers);
    ERHE_VULKAN_SYNC_TRACE(
        "[IMG_BARRIER] op=\"copy pre src\" image=0x{:x} old={} new={} src_stage={} dst_stage={}",
        reinterpret_cast<std::uintptr_t>(src_image),
        image_layout_str(src_barrier.oldLayout), image_layout_str(src_barrier.newLayout),
        pipeline_stage_flags_str(src_barrier.srcStageMask), pipeline_stage_flags_str(src_barrier.dstStageMask)
    );
    ERHE_VULKAN_SYNC_TRACE(
        "[IMG_BARRIER] op=\"copy pre dst\" image=0x{:x} old={} new={} src_stage={} dst_stage={}",
        reinterpret_cast<std::uintptr_t>(dst_image),
        image_layout_str(dst_barrier.oldLayout), image_layout_str(dst_barrier.newLayout),
        pipeline_stage_flags_str(dst_barrier.srcStageMask), pipeline_stage_flags_str(dst_barrier.dstStageMask)
    );

    const VkImageCopy region{
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), static_cast<uint32_t>(source_slice), 1},
        .srcOffset      = {source_origin.x, source_origin.y, source_origin.z},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(destination_level), static_cast<uint32_t>(destination_slice), 1},
        .dstOffset      = {destination_origin.x, destination_origin.y, destination_origin.z},
        .extent         = {static_cast<uint32_t>(source_size.x), static_cast<uint32_t>(source_size.y), static_cast<uint32_t>(source_size.z)}
    };
    vkCmdCopyImage(scope.cb, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition both back to shader read
    const VkImageMemoryBarrier2 src_post{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    const VkImageMemoryBarrier2 dst_post{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = dst_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(destination_level), 1, static_cast<uint32_t>(destination_slice), 1}
    };
    const VkImageMemoryBarrier2 post_barriers[] = {src_post, dst_post};
    cmd_pipeline_image_barriers2(scope.cb, 2, post_barriers);
    ERHE_VULKAN_SYNC_TRACE(
        "[IMG_BARRIER] op=\"copy post src\" image=0x{:x} old={} new={} src_stage={} dst_stage={}",
        reinterpret_cast<std::uintptr_t>(src_image),
        image_layout_str(src_post.oldLayout), image_layout_str(src_post.newLayout),
        pipeline_stage_flags_str(src_post.srcStageMask), pipeline_stage_flags_str(src_post.dstStageMask)
    );
    ERHE_VULKAN_SYNC_TRACE(
        "[IMG_BARRIER] op=\"copy post dst\" image=0x{:x} old={} new={} src_stage={} dst_stage={}",
        reinterpret_cast<std::uintptr_t>(dst_image),
        image_layout_str(dst_post.oldLayout), image_layout_str(dst_post.newLayout),
        pipeline_stage_flags_str(dst_post.srcStageMask), pipeline_stage_flags_str(dst_post.dstStageMask)
    );
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

    // bufferRowLength is in texels, not bytes; bufferImageHeight is in rows, not bytes
    const std::size_t bytes_per_pixel     = erhe::dataformat::get_format_size_bytes(destination_texture->get_pixelformat());
    const uint32_t    buffer_row_length   = (bytes_per_pixel > 0) ? static_cast<uint32_t>(source_bytes_per_row / bytes_per_pixel) : 0;
    const uint32_t    buffer_image_height = (source_bytes_per_row > 0) ? static_cast<uint32_t>(source_bytes_per_image / source_bytes_per_row) : 0;

    const VkBufferImageCopy region{
        .bufferOffset      = source_offset,
        .bufferRowLength   = buffer_row_length,
        .bufferImageHeight = buffer_image_height,
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

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    // Transition image to transfer destination layout
    const VkImageMemoryBarrier2 pre_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask       = 0,
        .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
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
    cmd_pipeline_image_barriers2(scope.cb, 1, &pre_barrier);
    ERHE_VULKAN_SYNC_TRACE(
        "[IMG_BARRIER] op=\"upload pre\" image=0x{:x} old={} new={} src_stage={} dst_stage={}",
        reinterpret_cast<std::uintptr_t>(vk_destination_image),
        image_layout_str(pre_barrier.oldLayout), image_layout_str(pre_barrier.newLayout),
        pipeline_stage_flags_str(pre_barrier.srcStageMask), pipeline_stage_flags_str(pre_barrier.dstStageMask)
    );

    vkCmdCopyBufferToImage(
        scope.cb,
        vk_source_buffer,
        vk_destination_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // Transition image to shader read layout
    const VkImageMemoryBarrier2 post_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
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
    cmd_pipeline_image_barriers2(scope.cb, 1, &post_barrier);
    ERHE_VULKAN_SYNC_TRACE(
        "[IMG_BARRIER] op=\"upload post\" image=0x{:x} old={} new={} src_stage={} dst_stage={}",
        reinterpret_cast<std::uintptr_t>(vk_destination_image),
        image_layout_str(post_barrier.oldLayout), image_layout_str(post_barrier.newLayout),
        pipeline_stage_flags_str(post_barrier.srcStageMask), pipeline_stage_flags_str(post_barrier.dstStageMask)
    );

    // Update tracked layout
    const_cast<Texture*>(destination_texture)->get_impl().set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    // Transition source to transfer src
    const VkImageMemoryBarrier2 pre_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    cmd_pipeline_image_barriers2(scope.cb, 1, &pre_barrier);

    // bufferRowLength is in texels, not bytes; bufferImageHeight is in rows, not bytes
    const std::size_t bytes_per_pixel     = erhe::dataformat::get_format_size_bytes(source_texture->get_pixelformat());
    const uint32_t    buffer_row_length   = (bytes_per_pixel > 0) ? static_cast<uint32_t>(destination_bytes_per_row / bytes_per_pixel) : 0;
    const uint32_t    buffer_image_height = (destination_bytes_per_row > 0) ? static_cast<uint32_t>(destination_bytes_per_image / destination_bytes_per_row) : 0;

    const VkBufferImageCopy region{
        .bufferOffset      = static_cast<VkDeviceSize>(destination_offset),
        .bufferRowLength   = buffer_row_length,
        .bufferImageHeight = buffer_image_height,
        .imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), static_cast<uint32_t>(source_slice), 1},
        .imageOffset       = {source_origin.x, source_origin.y, source_origin.z},
        .imageExtent       = {static_cast<uint32_t>(source_size.x), static_cast<uint32_t>(source_size.y), static_cast<uint32_t>(source_size.z)}
    };
    vkCmdCopyImageToBuffer(scope.cb, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_buffer, 1, &region);

    // Transition source back to shader read
    const VkImageMemoryBarrier2 post_barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = src_image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(source_level), 1, static_cast<uint32_t>(source_slice), 1}
    };
    cmd_pipeline_image_barriers2(scope.cb, 1, &post_barrier);
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

    // For texture views, use the view's base array layer offset
    const uint32_t base_layer = static_cast<uint32_t>(texture->get_impl().get_view_base_array_layer());

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    // Transition mip level 0 to transfer src
    const VkImageLayout current_layout = texture->get_impl().get_current_layout();
    const bool from_shader_read = (current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    {
        const VkImageMemoryBarrier2 barrier{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext               = nullptr,
            .srcStageMask        = from_shader_read ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .srcAccessMask       = from_shader_read ? VK_ACCESS_2_SHADER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout           = current_layout,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, base_layer, 1}
        };
        cmd_pipeline_image_barriers2(scope.cb, 1, &barrier);
    }

    int mip_width  = width;
    int mip_height = height;

    for (int i = 1; i < level_count; ++i) {
        int next_width  = (mip_width  > 1) ? (mip_width  / 2) : 1;
        int next_height = (mip_height > 1) ? (mip_height / 2) : 1;

        // Transition mip level i to transfer dst
        {
            const VkImageMemoryBarrier2 barrier{
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .srcAccessMask       = 0,
                .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = image,
                .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 1, base_layer, 1}
            };
            cmd_pipeline_image_barriers2(scope.cb, 1, &barrier);
        }

        // Blit from mip level i-1 to mip level i
        const VkImageBlit blit_region{
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i - 1), base_layer, 1},
            .srcOffsets     = {{0, 0, 0}, {mip_width, mip_height, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), base_layer, 1},
            .dstOffsets     = {{0, 0, 0}, {next_width, next_height, 1}}
        };
        vkCmdBlitImage(
            scope.cb,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit_region,
            VK_FILTER_LINEAR
        );

        // Transition mip level i from transfer dst to transfer src (for next iteration)
        {
            const VkImageMemoryBarrier2 barrier{
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = image,
                .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 1, base_layer, 1}
            };
            cmd_pipeline_image_barriers2(scope.cb, 1, &barrier);
        }

        mip_width  = next_width;
        mip_height = next_height;
    }

    // Transition all mip levels to shader read optimal
    {
        const VkImageMemoryBarrier2 barrier{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext               = nullptr,
            .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(level_count), base_layer, 1}
        };
        cmd_pipeline_image_barriers2(scope.cb, 1, &barrier);
    }

    // Update tracked layout
    const_cast<Texture*>(texture)->get_impl().set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    // vkCmdFillBuffer fills with a uint32_t value, replicate the byte across 4 bytes
    uint32_t fill_value =
         static_cast<uint32_t>(value       ) |
        (static_cast<uint32_t>(value) <<  8) |
        (static_cast<uint32_t>(value) << 16) |
        (static_cast<uint32_t>(value) << 24);

    vkCmdFillBuffer(
        scope.cb,
        buffer->get_impl().get_vk_buffer(),
        static_cast<VkDeviceSize>(offset),
        static_cast<VkDeviceSize>(length),
        fill_value
    );
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

    copy_from_texture(
        source_texture, 0, 0, destination_texture, 0, 0,
        static_cast<std::uintptr_t>(layer_count), static_cast<std::uintptr_t>(level_count)
    );
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

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    const VkBufferCopy region{
        .srcOffset = static_cast<VkDeviceSize>(source_offset),
        .dstOffset = static_cast<VkDeviceSize>(destination_offset),
        .size      = static_cast<VkDeviceSize>(size)
    };

    vkCmdCopyBuffer(
        scope.cb,
        source_buffer->get_impl().get_vk_buffer(),
        destination_buffer->get_impl().get_vk_buffer(),
        1,
        &region
    );
}

} // namespace erhe::graphics
