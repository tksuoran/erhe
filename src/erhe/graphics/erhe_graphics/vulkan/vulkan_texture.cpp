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
        case Texture_type::texture_1d:             return 1;
        case Texture_type::texture_2d:             return 2;
        case Texture_type::texture_2d_array:       return 2;
        case Texture_type::texture_cube_map:       return 2;
        case Texture_type::texture_cube_map_array: return 2;
        case Texture_type::texture_3d:             return 3;
        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

namespace {

auto to_vk_image_view_type(const Texture_type type, const int array_layer_count) -> VkImageViewType
{
    switch (type) {
        case Texture_type::texture_1d:             return (array_layer_count > 1) ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        case Texture_type::texture_2d:             return (array_layer_count > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        case Texture_type::texture_2d_array:       return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case Texture_type::texture_3d:             return VK_IMAGE_VIEW_TYPE_3D;
        case Texture_type::texture_cube_map:       return VK_IMAGE_VIEW_TYPE_CUBE;
        case Texture_type::texture_cube_map_array: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:                                   return VK_IMAGE_VIEW_TYPE_2D;
    }
}

} // anonymous namespace

Texture_impl::Texture_impl(Texture_impl&& other) noexcept
    : m_device             {other.m_device}
    , m_vma_allocation     {other.m_vma_allocation}
    , m_vk_image           {other.m_vk_image}
    , m_vk_image_view      {other.m_vk_image_view}
    , m_cached_image_views {std::move(other.m_cached_image_views)}
    , m_sample_count       {other.m_sample_count}
    , m_current_layout     {other.m_current_layout}
{
    other.m_vk_image       = VK_NULL_HANDLE;
    other.m_vk_image_view  = VK_NULL_HANDLE;
    other.m_vma_allocation = VK_NULL_HANDLE;
    other.m_current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

Texture_impl::~Texture_impl() noexcept
{
    Device_impl& device_impl = m_device.get_impl();

    // Collect all image views to destroy
    std::vector<VkImageView> image_views_to_destroy;
    for (const Cached_image_view& cached : m_cached_image_views) {
        if (cached.image_view != VK_NULL_HANDLE) {
            image_views_to_destroy.push_back(cached.image_view);
        }
    }
    m_cached_image_views.clear();

    if (m_vk_image_view != VK_NULL_HANDLE) {
        image_views_to_destroy.push_back(m_vk_image_view);
        m_vk_image_view = VK_NULL_HANDLE;
    }

    const VkImage       vk_image       = m_vk_image;
    const VmaAllocation vma_allocation = m_vma_allocation;
    m_vk_image       = VK_NULL_HANDLE;
    m_vma_allocation = VK_NULL_HANDLE;

    device_impl.add_completion_handler(
        [&device_impl, image_views_to_destroy = std::move(image_views_to_destroy), vk_image, vma_allocation]() {
            VkDevice vulkan_device = device_impl.get_vulkan_device();
            for (VkImageView view : image_views_to_destroy) {
                vkDestroyImageView(vulkan_device, view, nullptr);
            }
            if (vk_image != VK_NULL_HANDLE && vma_allocation != VK_NULL_HANDLE) {
                VmaAllocator& allocator = device_impl.get_allocator();
                vmaDestroyImage(allocator, vk_image, vma_allocation);
            }
        }
    );
}

Texture_impl::Texture_impl(Device& device, const Texture_create_info& create_info)
    : m_device                {device}
    , m_type                  {create_info.type}
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
    const bool is_cube = (m_type == Texture_type::texture_cube_map) || (m_type == Texture_type::texture_cube_map_array);
    const VkImageCreateInfo image_create_info{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = is_cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : VkImageCreateFlags{0},
        .imageType             = (m_type == Texture_type::texture_3d) ? VK_IMAGE_TYPE_3D
                               : (m_type == Texture_type::texture_1d) ? VK_IMAGE_TYPE_1D
                               :                                        VK_IMAGE_TYPE_2D,
        .format                = to_vulkan(create_info.pixelformat),
        .extent                = {
            .width  = static_cast<uint32_t>(create_info.width),
            .height = static_cast<uint32_t>(create_info.height),
            .depth  = static_cast<uint32_t>(create_info.depth)
        },
        .mipLevels             = static_cast<uint32_t>(m_level_count),
        .arrayLayers           = std::max(uint32_t{1}, static_cast<uint32_t>(m_array_layer_count)),
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
        log_swapchain->critical("vmaCreateImage() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    vmaSetAllocationName(allocator, m_vma_allocation, create_info.debug_label.data());

    const VkImageViewCreateInfo image_view_create_info{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = m_vk_image,
        .viewType         = to_vk_image_view_type(m_type, m_array_layer_count),
        .format           = image_create_info.format,
        .components       = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A
        },
        .subresourceRange = {
            .aspectMask = get_vulkan_image_aspect_flags(create_info.pixelformat),
            .levelCount = static_cast<uint32_t>(m_level_count),
            .layerCount = std::max(uint32_t{1}, static_cast<uint32_t>(m_array_layer_count))
        }
    };
    result = vkCreateImageView(vulkan_device, &image_view_create_info, nullptr, &m_vk_image_view);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateImageView() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    device.get_impl().set_debug_label(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_vk_image), m_debug_label.data());
    device.get_impl().set_debug_label(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_vk_image_view),
        fmt::format("{} default view", m_debug_label.data()).c_str());
}

auto Texture_impl::is_sparse() const -> bool
{
    return m_is_sparse;
}

auto Texture_impl::get_debug_label() const -> erhe::utility::Debug_label
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

void Texture_impl::set_buffer(Buffer& buffer)
{
    // TODO Implement texture buffer view for Vulkan (VkBufferView)
    m_buffer = &buffer;
}

void Texture_impl::clear() const
{
    // TODO Implement using vkCmdClearColorImage via immediate commands
}

auto Texture_impl::get_vma_allocation() const -> VmaAllocation
{
    return m_vma_allocation;
}

auto Texture_impl::get_vk_image() const -> VkImage
{
    return m_vk_image;
}

auto Texture_impl::get_vk_image_view() const -> VkImageView
{
    return m_vk_image_view;
}

auto Texture_impl::get_vk_image_view(
    const VkImageAspectFlags aspect_mask,
    const uint32_t           base_layer,
    const uint32_t           layer_count
) -> VkImageView
{
    return get_vk_image_view(aspect_mask, base_layer, layer_count, 0, static_cast<uint32_t>(m_level_count));
}

auto Texture_impl::get_vk_image_view(
    const VkImageAspectFlags aspect_mask,
    const uint32_t           base_layer,
    const uint32_t           layer_count,
    const uint32_t           base_level,
    const uint32_t           level_count
) -> VkImageView
{
    // Check cache
    for (const Cached_image_view& cached : m_cached_image_views) {
        if ((cached.aspect_mask == aspect_mask) &&
            (cached.base_layer  == base_layer) &&
            (cached.layer_count == layer_count) &&
            (cached.base_level  == base_level) &&
            (cached.level_count == level_count))
        {
            return cached.image_view;
        }
    }

    // Create new view
    VkDevice vulkan_device = m_device.get_impl().get_vulkan_device();
    VkFormat vk_format = to_vulkan(m_pixelformat);

    const VkImageViewCreateInfo image_view_create_info{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = m_vk_image,
        .viewType         = to_vk_image_view_type(m_type, m_array_layer_count),
        .format           = vk_format,
        .components       = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A
        },
        .subresourceRange = {
            .aspectMask     = aspect_mask,
            .baseMipLevel   = base_level,
            .levelCount     = level_count,
            .baseArrayLayer = base_layer,
            .layerCount     = layer_count
        }
    };

    VkImageView image_view = VK_NULL_HANDLE;
    VkResult result = vkCreateImageView(vulkan_device, &image_view_create_info, nullptr, &image_view);
    if (result != VK_SUCCESS) {
        log_texture->error("vkCreateImageView() for cached view failed with {}", static_cast<int32_t>(result));
        return VK_NULL_HANDLE;
    }

    m_cached_image_views.push_back(Cached_image_view{
        .aspect_mask = aspect_mask,
        .base_layer  = base_layer,
        .layer_count = layer_count,
        .base_level  = base_level,
        .level_count = level_count,
        .image_view  = image_view
    });

    m_device.get_impl().set_debug_label(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(image_view),
        fmt::format("{} view aspect={} layer={}-{} level={}-{}", m_debug_label.data(), aspect_mask, base_layer, base_layer + layer_count, base_level, base_level + level_count).c_str());

    return image_view;
}

auto Texture_impl::get_current_layout() const -> VkImageLayout
{
    return m_current_layout;
}

void Texture_impl::set_layout(VkImageLayout layout) const
{
    m_current_layout = layout;
}

void Texture_impl::transition_layout(VkCommandBuffer command_buffer, VkImageLayout new_layout) const
{
    if (m_vk_image == VK_NULL_HANDLE) {
        return;
    }
    if (m_current_layout == new_layout) {
        return;
    }

    // Determine access masks and pipeline stages based on old and new layouts
    VkAccessFlags src_access = 0;
    VkAccessFlags dst_access = 0;
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    switch (m_current_layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            src_access = 0;
            src_stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            src_stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            src_access = VK_ACCESS_TRANSFER_READ_BIT;
            src_stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            src_access = VK_ACCESS_SHADER_READ_BIT;
            src_stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            src_stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            src_stage  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        default:
            src_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            src_stage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
    }

    switch (new_layout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            dst_stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            dst_access = VK_ACCESS_TRANSFER_READ_BIT;
            dst_stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dst_access = VK_ACCESS_SHADER_READ_BIT;
            dst_stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dst_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dst_stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            dst_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dst_stage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            dst_access = VK_ACCESS_SHADER_READ_BIT;
            dst_stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            dst_access = 0;
            dst_stage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        default:
            dst_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            dst_stage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
    }

    // Determine aspect mask from format
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (erhe::dataformat::get_depth_size_bits(m_pixelformat) > 0) {
        aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (erhe::dataformat::get_stencil_size_bits(m_pixelformat) > 0) {
            aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }

    const VkImageMemoryBarrier barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = src_access,
        .dstAccessMask       = dst_access,
        .oldLayout           = m_current_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = m_vk_image,
        .subresourceRange    = {
            .aspectMask     = aspect_mask,
            .baseMipLevel   = 0,
            .levelCount     = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount     = VK_REMAINING_ARRAY_LAYERS
        }
    };

    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_current_layout = new_layout;
}

auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return
        (lhs.m_vma_allocation == rhs.m_vma_allocation) &&
        (lhs.m_vk_image       == rhs.m_vk_image      ) &&
        (lhs.m_vk_image_view  == rhs.m_vk_image_view ) &&
        (lhs.m_vk_sampler     == rhs.m_vk_sampler    );
}

auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
