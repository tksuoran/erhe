#pragma once

#include "erhe_graphics/surface.hpp"

#include "volk.h"

#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Device_impl;
class Surface_create_info;
class Swapchain;
class Vulkan_swapchain_create_info;

class Vulkan_swapchain_create_info
{
public:
    VkSwapchainCreateInfoKHR               swapchain_create_info;
    VkSwapchainPresentScalingCreateInfoKHR swapchain_present_scaling_create_info;
};

class Surface_impl final
{
public:
    Surface_impl(Device_impl& device, const Surface_create_info& create_info);
    ~Surface_impl() noexcept;

    [[nodiscard]] auto is_empty() -> bool;
    [[nodiscard]] auto is_valid() -> bool;
    [[nodiscard]] auto get_swapchain          () -> Swapchain*;
    [[nodiscard]] auto can_use_physical_device(VkPhysicalDevice physical_device) -> bool;
    [[nodiscard]] auto use_physical_device    (VkPhysicalDevice physical_device) -> bool;
    [[nodiscard]] auto get_surface_format     () -> VkSurfaceFormatKHR const;
    [[nodiscard]] auto get_present_mode       () -> VkPresentModeKHR const;
    [[nodiscard]] auto get_image_count        () -> uint32_t const;
    [[nodiscard]] auto get_vulkan_surface     () -> VkSurfaceKHR const;

    // Returns true if swapchain needs to be (re)created
    [[nodiscard]] auto update_swapchain(Vulkan_swapchain_create_info& out_swapchain_create_info) -> bool;

private:
    void fail();
    void choose_surface_format();
    void choose_present_mode  ();
    [[nodiscard]] auto get_surface_format_score        (VkSurfaceFormatKHR surface_format) const -> float;
    [[nodiscard]] auto get_present_mode_score          (VkPresentModeKHR present_mode) const -> float;
    [[nodiscard]] auto get_present_scaling_capabilities() const -> VkSurfacePresentScalingCapabilitiesKHR;

    Device_impl&                    m_device_impl;
    Surface_create_info             m_surface_create_info;
    VkPhysicalDevice                m_physical_device {VK_NULL_HANDLE};
    VkSurfaceKHR                    m_surface         {VK_NULL_HANDLE};
    std::vector<VkSurfaceFormatKHR> m_surface_formats;
    std::vector<VkPresentModeKHR>   m_present_modes;
    VkSurfaceFormatKHR              m_surface_format  {};
    VkPresentModeKHR                m_present_mode    {VK_PRESENT_MODE_FIFO_KHR};
    uint32_t                        m_image_count     {0};
    VkExtent2D                      m_swapchain_extent{0, 0};
    VkSwapchainKHR                  m_vulkan_swapchain{VK_NULL_HANDLE};
    std::unique_ptr<Swapchain>      m_swapchain;
    bool                            m_is_empty{true};
    bool                            m_is_valid{false};
};

} // namespace erhe::graphics

