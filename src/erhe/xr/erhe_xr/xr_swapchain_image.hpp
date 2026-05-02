#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <openxr/openxr.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Texture;
}

namespace erhe::xr {

class Swapchain;

class Swapchain_image
{
public:
    Swapchain_image (Swapchain* swapchain, const uint32_t image_index);
    ~Swapchain_image() noexcept;
    Swapchain_image (const Swapchain_image&) = delete;
    void operator=  (const Swapchain_image&) = delete;
    Swapchain_image (Swapchain_image&& other) noexcept;
    void operator=  (Swapchain_image&& other) noexcept;

    [[nodiscard]] auto get_image_index() const -> unsigned int;
    [[nodiscard]] auto get_texture    () const -> erhe::graphics::Texture*;

private:
    Swapchain* m_swapchain  {nullptr};
    uint32_t   m_image_index{0};
};

class Swapchain
{
public:
    // array_layer_count: 1 for a per-eye swapchain (default), 2+ for a
    // shared layered swapchain backing multiview rendering. The wrapped
    // texture is created as Texture_type::texture_2d_array when > 1, so
    // a multiview render pass can attach a 2D_ARRAY image view spanning
    // every layer.
    Swapchain(
        erhe::graphics::Device&        device,
        XrSwapchain                    xr_swapchain,
        erhe::dataformat::Format       pixelformat,
        uint32_t                       width,
        uint32_t                       height,
        uint32_t                       sample_count,
        uint32_t                       array_layer_count,
        uint64_t                       texture_usage_mask,
        const std::string&             debug_label
    );
    ~Swapchain() noexcept;
    Swapchain     (const Swapchain&) = delete;
    void operator=(const Swapchain&) = delete;
    Swapchain     (Swapchain&& other) noexcept;
    void operator=(Swapchain&& other) noexcept;

    [[nodiscard]] auto acquire           () -> std::optional<Swapchain_image>;
                  auto release           () -> bool;
    [[nodiscard]] auto wait              () -> bool;
    [[nodiscard]] auto get_texture       (const uint32_t image_index) const -> erhe::graphics::Texture*;
    [[nodiscard]] auto get_xr_swapchain  () const -> XrSwapchain;
    [[nodiscard]] auto get_array_layer_count() const -> uint32_t;

private:
    [[nodiscard]] auto enumerate_images(
        erhe::graphics::Device&        device,
        erhe::dataformat::Format       pixelformat,
        uint32_t                       width,
        uint32_t                       height,
        uint32_t                       sample_count,
        uint32_t                       array_layer_count,
        uint64_t                       texture_usage_mask,
        const std::string&             debug_label
    ) -> bool;

    XrSwapchain                                                m_xr_swapchain      {XR_NULL_HANDLE};
    uint32_t                                                   m_array_layer_count {1};
    std::vector<std::shared_ptr<erhe::graphics::Texture>>      m_textures;
};

}
