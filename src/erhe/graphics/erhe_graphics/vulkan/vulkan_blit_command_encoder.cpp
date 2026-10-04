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

// One subresource of a texture: its range relative to the texture (for the
// layout tracking) and its absolute level / layer in the VkImage (for the
// copy regions; a texture view adds its base level / layer).
class Image_subresource
{
public:
    Image_subresource_range range;
    uint32_t                image_level;
    uint32_t                image_layer;
    VkImageAspectFlags      aspect_mask;
};

auto get_subresource(const Texture_impl& texture_impl, const std::uintptr_t level, const std::uintptr_t layer) -> Image_subresource
{
    return Image_subresource{
        .range       = Image_subresource_range{
            .base_level  = static_cast<uint32_t>(level),
            .level_count = 1,
            .base_layer  = static_cast<uint32_t>(layer),
            .layer_count = 1
        },
        .image_level = static_cast<uint32_t>(level) + static_cast<uint32_t>(texture_impl.get_view_base_level()),
        .image_layer = static_cast<uint32_t>(layer) + static_cast<uint32_t>(texture_impl.get_view_base_array_layer()),
        .aspect_mask = get_vulkan_image_aspect_flags(texture_impl.get_pixelformat())
    };
}

// Returns a copy source to the layout it had before the copy, so a copy does
// not move the image into a layout its usage flags forbid. A source that was
// UNDEFINED stays in TRANSFER_SRC_OPTIMAL (no transition to UNDEFINED exists).
void restore_source_layout(
    const VkCommandBuffer          command_buffer,
    const Texture_impl&            texture_impl,
    const Image_subresource_range& range,
    const VkImageLayout            layout
)
{
    if (layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        texture_impl.transition_layout(command_buffer, range, layout);
    }
}

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

void Blit_command_encoder_impl::copy_from_texture(const Texture_location& source, glm::ivec3 source_size, const Texture_location& destination)
{
    const Texture* source_texture      = source.texture;
    std::uintptr_t source_slice        = source.slice;
    std::uintptr_t source_level        = source.level;
    glm::ivec3     source_origin       = source.origin;
    const Texture* destination_texture = destination.texture;
    std::uintptr_t destination_slice   = destination.slice;
    std::uintptr_t destination_level   = destination.level;
    glm::ivec3     destination_origin  = destination.origin;
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }

    const Texture_impl& source_impl      = source_texture->get_impl();
    const Texture_impl& destination_impl = destination_texture->get_impl();
    const Image_subresource source_subresource      = get_subresource(source_impl,      source_level,      source_slice);
    const Image_subresource destination_subresource = get_subresource(destination_impl, destination_level, destination_slice);

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    // Both transitions start from the tracked layout of the subresource. A
    // transition from UNDEFINED would permit the driver to discard the
    // destination texels outside the copied region.
    const VkImageLayout source_layout = source_impl.get_layout(source_subresource.range.base_level, source_subresource.range.base_layer);
    source_impl     .transition_layout(scope.cb, source_subresource.range,      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    destination_impl.transition_layout(scope.cb, destination_subresource.range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    const VkImageCopy region{
        .srcSubresource = {source_subresource.aspect_mask, source_subresource.image_level, source_subresource.image_layer, 1},
        .srcOffset      = {source_origin.x, source_origin.y, source_origin.z},
        .dstSubresource = {destination_subresource.aspect_mask, destination_subresource.image_level, destination_subresource.image_layer, 1},
        .dstOffset      = {destination_origin.x, destination_origin.y, destination_origin.z},
        .extent         = {static_cast<uint32_t>(source_size.x), static_cast<uint32_t>(source_size.y), static_cast<uint32_t>(source_size.z)}
    };
    vkCmdCopyImage(scope.cb, source_impl.get_vk_image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination_impl.get_vk_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    restore_source_layout(scope.cb, source_impl, source_subresource.range, source_layout);
    destination_impl.transition_layout(scope.cb, destination_subresource.range, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// Buffer to texture copy
void Blit_command_encoder_impl::copy_from_buffer(const Buffer_texel_location& source, glm::ivec3 source_size, const Texture_location& destination)
{
    const Buffer*  source_buffer          = source.buffer;
    std::uintptr_t source_offset          = source.offset;
    std::uintptr_t source_bytes_per_row   = source.bytes_per_row;
    std::uintptr_t source_bytes_per_image = source.bytes_per_image;
    const Texture* destination_texture    = destination.texture;
    std::uintptr_t destination_slice      = destination.slice;
    std::uintptr_t destination_level      = destination.level;
    glm::ivec3     destination_origin     = destination.origin;
    const VkBuffer          vk_source_buffer        = source_buffer->get_impl().get_vk_buffer();
    const Texture_impl&     destination_impl        = destination_texture->get_impl();
    const Image_subresource destination_subresource = get_subresource(destination_impl, destination_level, destination_slice);

    // bufferRowLength is in texels, not bytes; bufferImageHeight is in rows, not bytes.
    // For block-compressed destinations the source data is required to be tightly
    // packed; 0 tells Vulkan to derive the pitch from imageExtent.
    const erhe::dataformat::Format destination_format = destination_texture->get_pixelformat();
    const bool        is_compressed       = erhe::dataformat::is_block_compressed(destination_format);
    const std::size_t bytes_per_pixel     = erhe::dataformat::get_format_size_bytes(destination_format);
    const uint32_t    buffer_row_length   = (!is_compressed && (bytes_per_pixel > 0)) ? static_cast<uint32_t>(source_bytes_per_row / bytes_per_pixel) : 0;
    const uint32_t    buffer_image_height = (!is_compressed && (source_bytes_per_row > 0)) ? static_cast<uint32_t>(source_bytes_per_image / source_bytes_per_row) : 0;

    const VkBufferImageCopy region{
        .bufferOffset      = source_offset,
        .bufferRowLength   = buffer_row_length,
        .bufferImageHeight = buffer_image_height,
        .imageSubresource  = {
            .aspectMask     = destination_subresource.aspect_mask,
            .mipLevel       = destination_subresource.image_level,
            .baseArrayLayer = destination_subresource.image_layer,
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

    // From the tracked layout, not UNDEFINED: a region copy keeps the texels
    // outside the region.
    destination_impl.transition_layout(scope.cb, destination_subresource.range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(
        scope.cb,
        vk_source_buffer,
        destination_impl.get_vk_image(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    destination_impl.transition_layout(scope.cb, destination_subresource.range, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// Copy from texture to buffer
void Blit_command_encoder_impl::copy_from_texture(const Texture_location& source, glm::ivec3 source_size, const Buffer_texel_location& destination)
{
    const Texture* source_texture              = source.texture;
    std::uintptr_t source_slice                = source.slice;
    std::uintptr_t source_level                = source.level;
    glm::ivec3     source_origin               = source.origin;
    const Buffer*  destination_buffer          = destination.buffer;
    std::uintptr_t destination_offset          = destination.offset;
    std::uintptr_t destination_bytes_per_row   = destination.bytes_per_row;
    std::uintptr_t destination_bytes_per_image = destination.bytes_per_image;
    if ((source_texture == nullptr) || (destination_buffer == nullptr)) {
        return;
    }

    const Texture_impl&     source_impl        = source_texture->get_impl();
    const Image_subresource source_subresource = get_subresource(source_impl, source_level, source_slice);
    const VkBuffer          dst_buffer         = destination_buffer->get_impl().get_vk_buffer();

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    // The source's tracked layout is not necessarily SHADER_READ_ONLY: the
    // Id_renderer attachments are color_attachment|transfer_src (no sampled
    // usage) and the render pass leaves them in TRANSFER_SRC_OPTIMAL. The
    // tracked layout is restored afterwards, so the copy does not move the
    // image into a layout its usage flags forbid.
    const VkImageLayout source_layout = source_impl.get_layout(source_subresource.range.base_level, source_subresource.range.base_layer);
    source_impl.transition_layout(scope.cb, source_subresource.range, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // bufferRowLength is in texels, not bytes; bufferImageHeight is in rows, not bytes
    const std::size_t bytes_per_pixel     = erhe::dataformat::get_format_size_bytes(source_texture->get_pixelformat());
    const uint32_t    buffer_row_length   = (bytes_per_pixel > 0) ? static_cast<uint32_t>(destination_bytes_per_row / bytes_per_pixel) : 0;
    const uint32_t    buffer_image_height = (destination_bytes_per_row > 0) ? static_cast<uint32_t>(destination_bytes_per_image / destination_bytes_per_row) : 0;

    const VkBufferImageCopy region{
        .bufferOffset      = static_cast<VkDeviceSize>(destination_offset),
        .bufferRowLength   = buffer_row_length,
        .bufferImageHeight = buffer_image_height,
        .imageSubresource  = {source_subresource.aspect_mask, source_subresource.image_level, source_subresource.image_layer, 1},
        .imageOffset       = {source_origin.x, source_origin.y, source_origin.z},
        .imageExtent       = {static_cast<uint32_t>(source_size.x), static_cast<uint32_t>(source_size.y), static_cast<uint32_t>(source_size.z)}
    };
    vkCmdCopyImageToBuffer(scope.cb, source_impl.get_vk_image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_buffer, 1, &region);

    restore_source_layout(scope.cb, source_impl, source_subresource.range, source_layout);
}

void Blit_command_encoder_impl::generate_mipmaps(const Texture* texture)
{
    if (texture == nullptr) {
        return;
    }

    const Texture_impl&           texture_impl = texture->get_impl();
    const Image_subresource_range all          = texture_impl.get_all_subresources();
    if (all.level_count <= 1) {
        return;
    }

    const VkImage            image           = texture_impl.get_vk_image();
    const VkImageAspectFlags aspect_mask     = get_vulkan_image_aspect_flags(texture->get_pixelformat());
    const uint32_t           view_base_level = static_cast<uint32_t>(texture_impl.get_view_base_level());
    const uint32_t           view_base_layer = static_cast<uint32_t>(texture_impl.get_view_base_array_layer());

    Recording_scope scope{m_device.get_impl(), m_command_buffer};

    for (uint32_t layer = 0; layer < all.layer_count; ++layer) {
        texture_impl.transition_layout(scope.cb, Image_subresource_range{.base_level = 0, .level_count = 1, .base_layer = layer, .layer_count = 1}, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        int mip_width  = texture->get_width();
        int mip_height = texture->get_height();
        for (uint32_t level = 1; level < all.level_count; ++level) {
            const int next_width  = (mip_width  > 1) ? (mip_width  / 2) : 1;
            const int next_height = (mip_height > 1) ? (mip_height / 2) : 1;
            const Image_subresource_range level_range{.base_level = level, .level_count = 1, .base_layer = layer, .layer_count = 1};

            texture_impl.transition_layout(scope.cb, level_range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkImageBlit blit_region{
                .srcSubresource = {aspect_mask, view_base_level + level - 1, view_base_layer + layer, 1},
                .srcOffsets     = {{0, 0, 0}, {mip_width, mip_height, 1}},
                .dstSubresource = {aspect_mask, view_base_level + level,     view_base_layer + layer, 1},
                .dstOffsets     = {{0, 0, 0}, {next_width, next_height, 1}}
            };
            vkCmdBlitImage(
                scope.cb,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit_region,
                VK_FILTER_LINEAR
            );
            // Level i is the blit source of level i + 1
            texture_impl.transition_layout(scope.cb, level_range, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            mip_width  = next_width;
            mip_height = next_height;
        }
    }

    texture_impl.transition_layout(scope.cb, all, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
                erhe::graphics::Texture_location{.texture = source_texture, .slice = source_slice + slice, .level = source_level + level, .origin = glm::ivec3{0, 0, 0}},
                glm::ivec3{width, height, 1},
                erhe::graphics::Texture_location{.texture = destination_texture, .slice = destination_slice + slice, .level = destination_level + level, .origin = glm::ivec3{0, 0, 0}}
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
