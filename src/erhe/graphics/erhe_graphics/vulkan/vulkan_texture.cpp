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

#include <algorithm>
#include <array>
#include <cstring>

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

auto to_vk_image_view_type(const Texture_type type) -> VkImageViewType
{
    switch (type) {
        case Texture_type::texture_1d:             return VK_IMAGE_VIEW_TYPE_1D;
        case Texture_type::texture_2d:             return VK_IMAGE_VIEW_TYPE_2D;
        case Texture_type::texture_2d_array:       return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case Texture_type::texture_3d:             return VK_IMAGE_VIEW_TYPE_3D;
        case Texture_type::texture_cube_map:       return VK_IMAGE_VIEW_TYPE_CUBE;
        case Texture_type::texture_cube_map_array: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:                                   return VK_IMAGE_VIEW_TYPE_2D;
    }
}

} // anonymous namespace

Texture_impl::Texture_impl(Texture_impl&& other) noexcept
    : m_device_impl           {other.m_device_impl}
    , m_vma_allocation        {other.m_vma_allocation}
    , m_vk_image              {other.m_vk_image}
    , m_cached_image_views    {std::move(other.m_cached_image_views)}
    , m_is_view               {other.m_is_view}
    , m_view_base_array_layer {other.m_view_base_array_layer}
    , m_view_base_level       {other.m_view_base_level}
    , m_sample_count          {other.m_sample_count}
    , m_layout_state          {std::move(other.m_layout_state)}
{
    other.m_vk_image       = VK_NULL_HANDLE;
    other.m_vma_allocation = VK_NULL_HANDLE;
    other.m_is_view        = false;
}

Texture_impl::~Texture_impl() noexcept
{
    // Collect all image views to destroy
    std::vector<VkImageView> image_views_to_destroy;
    for (const Cached_image_view& cached : m_cached_image_views) {
        if (cached.image_view != VK_NULL_HANDLE) {
            image_views_to_destroy.push_back(cached.image_view);
        }
    }
    m_cached_image_views.clear();

    const VkImage       vk_image       = m_vk_image;
    const VmaAllocation vma_allocation = m_vma_allocation;
    m_vk_image       = VK_NULL_HANDLE;
    m_vma_allocation = VK_NULL_HANDLE;

    m_device_impl.add_completion_handler(
        [image_views_to_destroy = std::move(image_views_to_destroy), vk_image, vma_allocation](Device_impl& device_impl) {
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
    : m_device_impl           {device.get_impl()}
    , m_type                  {create_info.type}
    , m_pixelformat           {create_info.pixelformat}
    , m_fixed_sample_locations{create_info.fixed_sample_locations}
    , m_view_base_array_layer {create_info.view_base_array_layer}
    , m_view_base_level       {create_info.view_base_level}
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
    ERHE_VERIFY(m_width > 0);
    ERHE_VERIFY(m_height > 0);
    // Texture view: share the source texture's VkImage. We do not create any
    // VkImageView eagerly here; callers obtain views via the explicit
    // get_vk_image_view(aspect_mask, ...) overload.
    if (create_info.view_source) {
        Texture_impl& source_impl = create_info.view_source->get_impl();
        m_vk_image       = source_impl.get_vk_image();
        m_vma_allocation = VK_NULL_HANDLE; // no separate allocation for views
        m_is_view        = true;
        m_layout_state   = source_impl.m_layout_state;
        ERHE_VERIFY(m_layout_state);
        // The view's extents are those of the image level it starts at, and
        // its levels and layers lie inside the image: get_width() and
        // get_all_subresources() of a view describe exactly what it covers
        // (Texture_create_info::make_view builds such a create_info).
        const unsigned int base_level = static_cast<unsigned int>(m_view_base_level);
        ERHE_VERIFY(m_width  == source_impl.get_width (base_level));
        ERHE_VERIFY(m_height == source_impl.get_height(base_level));
        ERHE_VERIFY(m_depth  == source_impl.get_depth (base_level));
        ERHE_VERIFY(static_cast<uint32_t>(m_view_base_level) + static_cast<uint32_t>(m_level_count) <= m_layout_state->get_level_count());
        ERHE_VERIFY(static_cast<uint32_t>(m_view_base_array_layer) + std::max(uint32_t{1}, static_cast<uint32_t>(m_array_layer_count)) <= m_layout_state->get_layer_count());
        return;
    }

    // Externally-owned VkImage (e.g. OpenXR swapchain image). The image storage
    // lifetime is managed by the owner (xrDestroySwapchain), so we skip
    // vmaCreateImage and leave m_vma_allocation == VK_NULL_HANDLE. The
    // destructor only destroys the image view(s) and leaves m_vk_image alone
    // when vma_allocation is null.
    if (create_info.wrap_texture_name != 0) {
        static_assert(sizeof(VkImage) == sizeof(uint64_t), "VkImage expected to be 64 bits wide");
        VkImage wrapped_image{VK_NULL_HANDLE};
        std::memcpy(&wrapped_image, &create_info.wrap_texture_name, sizeof(VkImage));
        m_vk_image       = wrapped_image;
        m_vma_allocation = VK_NULL_HANDLE;
        m_is_view        = true; // treat as non-owning
        m_layout_state   = std::make_shared<Image_layout_state>(
            static_cast<uint32_t>(m_level_count),
            std::max(uint32_t{1}, static_cast<uint32_t>(m_array_layer_count))
        );
        device.get_impl().set_debug_label(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_vk_image), m_debug_label.data());
        return;
    }

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

    VmaAllocator& allocator = device.get_impl().get_allocator();
    VkResult result = vmaCreateImage(allocator, &image_create_info, &allocation_create_info, &m_vk_image, &m_vma_allocation, nullptr);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vmaCreateImage() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    vmaSetAllocationName(allocator, m_vma_allocation, create_info.debug_label.data());
    device.get_impl().set_debug_label(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_vk_image), m_debug_label.data());
    m_layout_state = std::make_shared<Image_layout_state>(image_create_info.mipLevels, image_create_info.arrayLayers);
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

auto Texture_impl::get_view_base_array_layer() const -> int
{
    return m_view_base_array_layer;
}

auto Texture_impl::get_view_base_level() const -> int
{
    return m_view_base_level;
}

auto Texture_impl::get_vk_image_view(
    const VkImageAspectFlags aspect_mask,
    const uint32_t           base_layer,
    const uint32_t           layer_count
) -> VkImageView
{
    // Sampling / storage views derive the view type from the texture's native
    // type (cube, cube-array, 3D, ...). Render-pass attachment views must instead
    // use a 2D-family type and call the explicit-view-type overload below.
    return get_vk_image_view(aspect_mask, base_layer, layer_count, 0, static_cast<uint32_t>(m_level_count), to_vk_image_view_type(m_type));
}

auto Texture_impl::get_vk_image_view(
    const VkImageAspectFlags aspect_mask,
    const uint32_t           base_layer,
    const uint32_t           layer_count,
    const uint32_t           base_level,
    const uint32_t           level_count,
    const VkImageViewType    view_type
) -> VkImageView
{
    // Check cache
    for (const Cached_image_view& cached : m_cached_image_views) {
        if ((cached.aspect_mask == aspect_mask) &&
            (cached.base_layer  == base_layer) &&
            (cached.layer_count == layer_count) &&
            (cached.base_level  == base_level) &&
            (cached.level_count == level_count) &&
            (cached.view_type   == view_type))
        {
            return cached.image_view;
        }
    }

    // Create new view - offset by view's own base layer/level for texture views
    const uint32_t actual_base_layer = base_layer + static_cast<uint32_t>(m_view_base_array_layer);
    const uint32_t actual_base_level = base_level + static_cast<uint32_t>(m_view_base_level);

    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkFormat vk_format = to_vulkan(m_pixelformat);

    const VkImageViewCreateInfo image_view_create_info{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = m_vk_image,
        .viewType         = view_type,
        .format           = vk_format,
        .components       = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A
        },
        .subresourceRange = {
            .aspectMask     = aspect_mask,
            .baseMipLevel   = actual_base_level,
            .levelCount     = level_count,
            .baseArrayLayer = actual_base_layer,
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
        .view_type   = view_type,
        .image_view  = image_view
    });

    m_device_impl.set_debug_label(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(image_view),
        fmt::format("{} view aspect={} layer={}-{} level={}-{}", m_debug_label.data(), aspect_mask, actual_base_layer, actual_base_layer + layer_count, actual_base_level, actual_base_level + level_count).c_str());

    return image_view;
}

Image_layout_state::Image_layout_state(const uint32_t level_count, const uint32_t layer_count)
    : m_level_count{level_count}
    , m_layer_count{layer_count}
    , m_layouts    (static_cast<std::size_t>(level_count) * static_cast<std::size_t>(layer_count), VK_IMAGE_LAYOUT_UNDEFINED)
{
}

auto Image_layout_state::get_level_count() const -> uint32_t
{
    return m_level_count;
}

auto Image_layout_state::get_layer_count() const -> uint32_t
{
    return m_layer_count;
}

auto Image_layout_state::get_layout(const uint32_t level, const uint32_t layer) const -> VkImageLayout
{
    ERHE_VERIFY((level < m_level_count) && (layer < m_layer_count));
    return m_layouts[(static_cast<std::size_t>(layer) * m_level_count) + level];
}

void Image_layout_state::set_layout(const uint32_t level, const uint32_t layer, const VkImageLayout layout)
{
    ERHE_VERIFY((level < m_level_count) && (layer < m_layer_count));
    m_layouts[(static_cast<std::size_t>(layer) * m_level_count) + level] = layout;
}

namespace {

class Layout_scope
{
public:
    VkPipelineStageFlags2 stage;
    VkAccessFlags2        access;
};

// A shader-read-only texture may be sampled by any shader stage, not just the
// fragment stage (e.g. the sky multi-scatter compute pass samples the
// transmittance LUT). Scoping read-only transitions to FRAGMENT_SHADER alone
// leaves compute (and vertex/geometry) consumers unsynchronized, which sync
// validation flags as a READ_AFTER_WRITE hazard. Matches the texture-fetch
// scope used by Command_buffer_impl::memory_barrier().
constexpr VkPipelineStageFlags2 any_shader_stage =
    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

// The accesses that may have happened to a subresource while it was in layout
// (the source scope of a transition out of it).
auto get_source_scope(const VkImageLayout layout) -> Layout_scope
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:                        return {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,             0};
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,            VK_ACCESS_2_TRANSFER_WRITE_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,            VK_ACCESS_2_TRANSFER_READ_BIT};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         return {any_shader_stage,                                VK_ACCESS_2_SHADER_READ_BIT};
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return {VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        default:                                               return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }
}

// The accesses that will happen to a subresource in layout (the destination
// scope of a transition into it).
auto get_destination_scope(const VkImageLayout layout) -> Layout_scope
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,             VK_ACCESS_2_TRANSFER_WRITE_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,             VK_ACCESS_2_TRANSFER_READ_BIT};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         return {any_shader_stage,                                 VK_ACCESS_2_SHADER_READ_BIT};
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:  return {any_shader_stage,                                 VK_ACCESS_2_SHADER_READ_BIT};
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:                  return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,           0};
        default:                                               return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,             VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }
}

} // anonymous namespace

auto Texture_impl::get_all_subresources() const -> Image_subresource_range
{
    // Exact for a view too: the constructor verified that the view's levels
    // and layers lie inside the image.
    return Image_subresource_range{
        .base_level  = 0,
        .level_count = static_cast<uint32_t>(m_level_count),
        .base_layer  = 0,
        .layer_count = std::max(uint32_t{1}, static_cast<uint32_t>(m_array_layer_count))
    };
}

auto Texture_impl::get_layout(const uint32_t level, const uint32_t layer) const -> VkImageLayout
{
    return m_layout_state->get_layout(
        level + static_cast<uint32_t>(m_view_base_level),
        layer + static_cast<uint32_t>(m_view_base_array_layer)
    );
}

auto Texture_impl::is_layout_undefined(const Image_subresource_range& range) const -> bool
{
    for (uint32_t layer = range.base_layer, layer_end = range.base_layer + range.layer_count; layer < layer_end; ++layer) {
        for (uint32_t level = range.base_level, level_end = range.base_level + range.level_count; level < level_end; ++level) {
            if (get_layout(level, layer) != VK_IMAGE_LAYOUT_UNDEFINED) {
                return false;
            }
        }
    }
    return true;
}

void Texture_impl::set_layout(const Image_subresource_range& range, const VkImageLayout layout) const
{
    for (uint32_t layer = range.base_layer, layer_end = range.base_layer + range.layer_count; layer < layer_end; ++layer) {
        for (uint32_t level = range.base_level, level_end = range.base_level + range.level_count; level < level_end; ++level) {
            m_layout_state->set_layout(
                level + static_cast<uint32_t>(m_view_base_level),
                layer + static_cast<uint32_t>(m_view_base_array_layer),
                layout
            );
        }
    }
}

void Texture_impl::transition_layout(const VkCommandBuffer command_buffer, const VkImageLayout new_layout) const
{
    transition_layout(command_buffer, get_all_subresources(), new_layout);
}

void Texture_impl::transition_layout(const VkCommandBuffer command_buffer, const Image_subresource_range& range, const VkImageLayout new_layout) const
{
    if (m_vk_image == VK_NULL_HANDLE) {
        return;
    }
    // A transition to UNDEFINED is invalid (VUID-VkImageMemoryBarrier2-newLayout-01198)
    ERHE_VERIFY((new_layout != VK_IMAGE_LAYOUT_UNDEFINED) && (new_layout != VK_IMAGE_LAYOUT_PREINITIALIZED));

    const Layout_scope       destination_scope = get_destination_scope(new_layout);
    const VkImageAspectFlags aspect_mask       = get_vulkan_image_aspect_flags(m_pixelformat);
    const uint32_t           view_base_level   = static_cast<uint32_t>(m_view_base_level);
    const uint32_t           view_base_layer   = static_cast<uint32_t>(m_view_base_array_layer);

    // One barrier per run of consecutive levels sharing their old layout,
    // per layer; flushed in fixed-size batches so no allocation happens.
    constexpr std::size_t batch_capacity = 16;
    std::array<VkImageMemoryBarrier2, batch_capacity> batch;
    std::size_t batch_size = 0;
    const auto flush = [&]() {
        if (batch_size > 0) {
            cmd_pipeline_image_barriers2(command_buffer, static_cast<uint32_t>(batch_size), batch.data());
            batch_size = 0;
        }
    };
    const auto add_barrier = [&](const VkImageLayout old_layout, const uint32_t base_level, const uint32_t level_count, const uint32_t base_layer, const uint32_t layer_count) {
        const Layout_scope source_scope = get_source_scope(old_layout);
        batch[batch_size++] = VkImageMemoryBarrier2{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext               = nullptr,
            .srcStageMask        = source_scope.stage,
            .srcAccessMask       = source_scope.access,
            .dstStageMask        = destination_scope.stage,
            .dstAccessMask       = destination_scope.access,
            .oldLayout           = old_layout,
            .newLayout           = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = m_vk_image,
            .subresourceRange    = {
                .aspectMask     = aspect_mask,
                .baseMipLevel   = view_base_level + base_level,
                .levelCount     = level_count,
                .baseArrayLayer = view_base_layer + base_layer,
                .layerCount     = layer_count
            }
        };
        ERHE_VULKAN_SYNC_TRACE(
            "[IMG_BARRIER] op=\"transition\" image=0x{:x} levels={}+{} layers={}+{} old={} new={} src_stage={} dst_stage={}",
            reinterpret_cast<std::uintptr_t>(m_vk_image),
            view_base_level + base_level, level_count, view_base_layer + base_layer, layer_count,
            image_layout_str(old_layout),
            image_layout_str(new_layout),
            pipeline_stage_flags_str(source_scope.stage),
            pipeline_stage_flags_str(destination_scope.stage)
        );
        if (batch_size == batch_capacity) {
            flush();
        }
    };

    const uint32_t level_end = range.base_level + range.level_count;
    const uint32_t layer_end = range.base_layer + range.layer_count;

    // Common case: the whole range shares one layout -> a single barrier.
    const VkImageLayout first_layout = get_layout(range.base_level, range.base_layer);
    bool uniform = true;
    for (uint32_t layer = range.base_layer; uniform && (layer < layer_end); ++layer) {
        for (uint32_t level = range.base_level; level < level_end; ++level) {
            if (get_layout(level, layer) != first_layout) {
                uniform = false;
                break;
            }
        }
    }
    if (uniform) {
        if (first_layout != new_layout) {
            add_barrier(first_layout, range.base_level, range.level_count, range.base_layer, range.layer_count);
        }
    } else {
        for (uint32_t layer = range.base_layer; layer < layer_end; ++layer) {
            uint32_t run_begin = range.base_level;
            while (run_begin < level_end) {
                const VkImageLayout run_layout = get_layout(run_begin, layer);
                uint32_t run_end = run_begin + 1;
                while ((run_end < level_end) && (get_layout(run_end, layer) == run_layout)) {
                    ++run_end;
                }
                if (run_layout != new_layout) {
                    add_barrier(run_layout, run_begin, run_end - run_begin, layer, 1);
                }
                run_begin = run_end;
            }
        }
    }
    flush();
    set_layout(range, new_layout);
}

auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return
        (lhs.m_vma_allocation == rhs.m_vma_allocation) &&
        (lhs.m_vk_image       == rhs.m_vk_image      ) &&
        (lhs.m_vk_sampler     == rhs.m_vk_sampler    );
}

auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
