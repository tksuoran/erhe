#pragma once

#include <openxr/openxr.h>

#include <optional>
#include <vector>

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
    [[nodiscard]] auto get_gl_texture () const -> unsigned int;

private:
    Swapchain* m_swapchain  {nullptr};
    uint32_t   m_image_index{0};
};

class Swapchain
{
public:
    explicit Swapchain(XrSwapchain xr_swapchain);
    ~Swapchain() noexcept;
    Swapchain     (const Swapchain&) = delete;
    void operator=(const Swapchain&) = delete;
    Swapchain     (Swapchain&& other) noexcept;
    void operator=(Swapchain&& other) noexcept;

    [[nodiscard]] auto acquire         () -> std::optional<Swapchain_image>;
                  auto release         () -> bool;
    [[nodiscard]] auto wait            () -> bool;
    [[nodiscard]] auto get_gl_texture  (const uint32_t image_index) const -> unsigned int;
    [[nodiscard]] auto get_xr_swapchain() const -> XrSwapchain;

private:
    [[nodiscard]] auto enumerate_images() -> bool;

    XrSwapchain               m_xr_swapchain{XR_NULL_HANDLE};
    std::vector<unsigned int> m_gl_textures;
};

}
