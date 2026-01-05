// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

auto Texture_impl::get_mipmap_dimensions(const Texture_type type) -> int
{
    switch (type) {
        //using enum gl::Texture_target;
        case Texture_type::texture_1d:       return 1;
        case Texture_type::texture_cube_map: return 2;
        case Texture_type::texture_2d:       return 2;
        case Texture_type::texture_3d:       return 3;
        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

Texture_impl::Texture_impl(Texture_impl&&) noexcept
{
    ERHE_FATAL("Not implemented");
}

Texture_impl::~Texture_impl() noexcept
{
}

Texture_impl::Texture_impl(Device& device, const Texture_create_info& create_info)
    : m_type                  {create_info.type}
    , m_pixelformat           {create_info.pixelformat}
    , m_fixed_sample_locations{create_info.fixed_sample_locations}
    , m_sample_count          {create_info.sample_count}
    , m_width                 {create_info.width}
    , m_height                {create_info.height}
    , m_depth                 {create_info.depth}
    , m_array_layer_count     {create_info.array_layer_count}
    , m_level_count           {
        (create_info.level_count != 0)
            ? create_info.level_count
            : create_info.get_texture_level_count()
    }
    , m_buffer                {create_info.buffer}
    , m_debug_label           {create_info.debug_label}
{
    const VkImageCreateInfo image_create_info{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = to_vulkan(create_info.pixelformat),
        .extent                = {
            .width  = static_cast<uint32_t>(create_info.width),
            .height = static_cast<uint32_t>(create_info.height),
            .depth  = static_cast<uint32_t>(create_info.depth)
        },
        .mipLevels             = static_cast<uint32_t>(create_info.level_count),
        .arrayLayers           = static_cast<uint32_t>(create_info.array_layer_count),
        .samples               = get_vulkan_sample_count(create_info.sample_count),
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = get_vulkan_image_usage_flags(create_info.usage_mask),
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
    };

    //const bool transfer_dst = erhe::utility::test_bit_set(create_info.usage_mask, Image_usage_flag_bit_mask::transfer_dst);
    // TODO .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
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

    Device_impl&  device_impl   = device.get_impl();
    VkDevice      vulkan_device = device_impl.get_vulkan_device();
    VmaAllocator& allocator     = device.get_impl().get_allocator();
    VkResult      result        = VK_SUCCESS;

    result = vmaCreateImage(allocator, &image_create_info, &allocation_create_info, &m_vk_image, &m_vma_allocation, nullptr);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkDeviceWaitIdle() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    const VkImageViewCreateInfo image_view_create_info{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = m_vk_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = image_create_info.format,
        .components       = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A
        },
        .subresourceRange = {
            .aspectMask = get_vulkan_image_aspect_flags(create_info.pixelformat),
            .levelCount = static_cast<uint32_t>(create_info.level_count),
            .layerCount = static_cast<uint32_t>(create_info.array_layer_count)
        }
    };
    result = vkCreateImageView(vulkan_device, &image_view_create_info, nullptr, &m_vk_image_view);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkDeviceWaitIdle() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
}

auto Texture_impl::is_sparse() const -> bool
{
    return m_is_sparse;
}

auto Texture_impl::get_debug_label() const -> const std::string&
{
    return m_debug_label;
}

auto Texture_impl::get_texture_type() const -> Texture_type
{
    return m_type;
}

auto Texture_impl::is_layered() const -> bool
{
    return false;
}

auto Texture_impl::get_width(unsigned int level) const -> int
{
    int size = m_width;
    for (unsigned int i = 0; i < level; i++) {
        size = std::max(1, size / 2);
    }
    return size;
}

auto Texture_impl::get_height(unsigned int level) const -> int
{
    int size = m_height;
    for (unsigned int i = 0; i < level; i++) {
        size = std::max(1, size / 2);
    }
    return size;
}

auto Texture_impl::get_depth(unsigned int level) const -> int
{
    int size = m_depth;
    for (unsigned int i = 0; i < level; i++) {
        size = std::max(1, size / 2);
    }
    return size;
}

auto Texture_impl::get_array_layer_count() const -> int
{
    return m_array_layer_count;
}

auto Texture_impl::get_level_count() const -> int
{
    return m_level_count;
}

auto Texture_impl::get_fixed_sample_locations() const -> bool
{
    return m_fixed_sample_locations;
}

auto Texture_impl::get_pixelformat() const -> erhe::dataformat::Format
{
    return m_pixelformat;
}

auto Texture_impl::get_sample_count() const -> int
{
    return m_sample_count;
}

auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    static_cast<void>(rhs);
    static_cast<void>(lhs);
    return false;
}

auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
