#pragma once

#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_utility/debug_label.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics {

class Device_impl;

// A range of mip levels and array layers, in the coordinates of one
// Texture_impl: a texture view adds its own base level / layer offset.
class Image_subresource_range
{
public:
    uint32_t base_level {0};
    uint32_t level_count{1};
    uint32_t base_layer {0};
    uint32_t layer_count{1};
};

// The current VkImageLayout of every (level, layer) subresource of one
// VkImage. Shared by the texture that owns (or wraps) the image and every
// texture view of it, so a transition recorded through any of them is seen
// by all.
class Image_layout_state final
{
public:
    Image_layout_state(uint32_t level_count, uint32_t layer_count);

    [[nodiscard]] auto get_level_count() const -> uint32_t;
    [[nodiscard]] auto get_layer_count() const -> uint32_t;
    [[nodiscard]] auto get_layout     (uint32_t level, uint32_t layer) const -> VkImageLayout;
    void set_layout(uint32_t level, uint32_t layer, VkImageLayout layout);

private:
    uint32_t                   m_level_count{0};
    uint32_t                   m_layer_count{0};
    std::vector<VkImageLayout> m_layouts; // index: layer * m_level_count + level
};

class Texture_impl final
{
public:
    Texture_impl           (const Texture_impl&) = delete;
    Texture_impl& operator=(const Texture_impl&) = delete;
    Texture_impl(Texture_impl&&) noexcept;
    Texture_impl& operator=(Texture_impl&&) = delete;
    ~Texture_impl() noexcept;

    Texture_impl(Device& device, const Texture_create_info& create_info);

    using Create_info = Texture_create_info;

    [[nodiscard]] static auto get_mipmap_dimensions(Texture_type type) -> int;

    [[nodiscard]] auto get_debug_label           () const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_pixelformat           () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_width                 (unsigned int level = 0) const -> int;
    [[nodiscard]] auto get_height                (unsigned int level = 0) const -> int;
    [[nodiscard]] auto get_depth                 (unsigned int level = 0) const -> int;
    [[nodiscard]] auto get_array_layer_count     () const -> int;
    [[nodiscard]] auto get_level_count           () const -> int;
    [[nodiscard]] auto get_fixed_sample_locations() const -> bool;
    [[nodiscard]] auto get_sample_count          () const -> int;
    [[nodiscard]] auto get_texture_type          () const -> Texture_type;
    [[nodiscard]] auto is_layered                () const -> bool;
    [[nodiscard]] auto is_sparse                 () const -> bool;

    [[nodiscard]] auto get_vma_allocation        () const -> VmaAllocation;
    [[nodiscard]] auto get_vk_image              () const -> VkImage;
    [[nodiscard]] auto get_view_base_array_layer () const -> int;
    [[nodiscard]] auto get_view_base_level       () const -> int;
    [[nodiscard]] auto get_vk_image_view         (VkImageAspectFlags aspect_mask, uint32_t base_layer, uint32_t layer_count) -> VkImageView;
    [[nodiscard]] auto get_vk_image_view         (VkImageAspectFlags aspect_mask, uint32_t base_layer, uint32_t layer_count, uint32_t base_level, uint32_t level_count, VkImageViewType view_type) -> VkImageView;

    // Image layout tracking, per subresource. Ranges are relative to this
    // texture (see Image_subresource_range).
    [[nodiscard]] auto get_all_subresources() const -> Image_subresource_range;
    [[nodiscard]] auto get_layout          (uint32_t level, uint32_t layer) const -> VkImageLayout;
    [[nodiscard]] auto is_layout_undefined (const Image_subresource_range& range) const -> bool;

    // Records a barrier moving every subresource of the range that is not
    // already in new_layout from its tracked layout to new_layout.
    void transition_layout(VkCommandBuffer command_buffer, const Image_subresource_range& range, VkImageLayout new_layout) const;
    void transition_layout(VkCommandBuffer command_buffer, VkImageLayout new_layout) const;
    // Updates the tracked layout without a barrier, for transitions applied
    // by other means (render pass finalLayout).
    void set_layout       (const Image_subresource_range& range, VkImageLayout layout) const;

    void clear() const;
    void set_buffer       (Buffer& buffer);

private:
    friend bool operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept;

    class Cached_image_view
    {
    public:
        VkImageAspectFlags aspect_mask{0};
        uint32_t           base_layer {0};
        uint32_t           layer_count{0};
        uint32_t           base_level {0};
        uint32_t           level_count{0};
        VkImageViewType    view_type  {VK_IMAGE_VIEW_TYPE_2D};
        VkImageView        image_view {VK_NULL_HANDLE};
    };

    Device_impl&                   m_device_impl;
    VmaAllocation                  m_vma_allocation{VK_NULL_HANDLE};
    VkImage                        m_vk_image      {VK_NULL_HANDLE};
    VkSampler                      m_vk_sampler    {VK_NULL_HANDLE};
    std::vector<Cached_image_view> m_cached_image_views;

    Texture_type               m_type                  {Texture_type::texture_2d};
    erhe::dataformat::Format   m_pixelformat           {erhe::dataformat::Format::format_8_vec4_srgb};
    bool                       m_fixed_sample_locations{true};
    bool                       m_is_sparse             {false};
    bool                       m_is_view               {false};
    int                        m_view_base_array_layer {0};
    int                        m_view_base_level       {0};
    int                        m_sample_count          {0};
    int                        m_width                 {0};
    int                        m_height                {0};
    int                        m_depth                 {0};
    int                        m_array_layer_count     {0};
    int                        m_level_count           {0};
    Buffer*                    m_buffer                {nullptr};
    erhe::utility::Debug_label m_debug_label;
    std::shared_ptr<Image_layout_state> m_layout_state;
};


class Texture_impl_hash
{
public:
    auto operator()(const Texture_impl& texture) const noexcept -> size_t
    {
        static_cast<void>(texture);
        return 0;
    }
};

[[nodiscard]] auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool;
 

} // namespace erhe::graphics
