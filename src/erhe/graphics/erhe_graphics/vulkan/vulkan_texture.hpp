#pragma once

#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

namespace erhe::graphics {

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

    [[nodiscard]] static auto get_mipmap_dimensions (Texture_type type) -> int;

    [[nodiscard]] auto get_debug_label           () const -> const std::string&;
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
    [[nodiscard]] auto get_handle                () const -> uint64_t;
    [[nodiscard]] auto is_sparse                 () const -> bool;

private:
    VmaAllocation m_vma_allocation{VK_NULL_HANDLE};
    VkImage       m_vk_image      {VK_NULL_HANDLE};	
    VkImageView   m_vk_image_view {VK_NULL_HANDLE};
    VkSampler     m_vk_sampler    {VK_NULL_HANDLE};

    Texture_type             m_type                  {Texture_type::texture_2d};
    erhe::dataformat::Format m_pixelformat           {erhe::dataformat::Format::format_8_vec4_srgb};
    bool                     m_fixed_sample_locations{true};
    bool                     m_is_sparse             {false};
    int                      m_sample_count          {0};
    int                      m_width                 {0};
    int                      m_height                {0};
    int                      m_depth                 {0};
    int                      m_array_layer_count     {0};
    int                      m_level_count           {0};
    Buffer*                  m_buffer                {nullptr};
    std::string              m_debug_label;
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
