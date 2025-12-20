#pragma once

#include "erhe_graphics/surface.hpp"

#include "volk.h"

#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Device_impl;
class Surface_create_info;
class Swapchain;

class Surface_impl final
{
public:
    Surface_impl(Device_impl& device, const Surface_create_info& create_info);
    ~Surface_impl() noexcept;

    [[nodiscard]] auto get_swapchain          () -> Swapchain*;
    [[nodiscard]] auto can_use_physical_device(VkPhysicalDevice physical_device) -> bool;
    [[nodiscard]] auto use_physical_device    (VkPhysicalDevice physical_device) -> bool;
    [[nodiscard]] auto get_surface_format     () -> VkSurfaceFormatKHR const;
    [[nodiscard]] auto get_present_mode       () -> VkPresentModeKHR const;
    [[nodiscard]] auto get_image_count        () -> uint32_t const;
    [[nodiscard]] auto get_vulkan_surface     () -> VkSurfaceKHR const;

    void resize_swapchain_to_surface(uint32_t& out_width, uint32_t& out_height);

    void update_swapchain(
        bool&                     error,
        bool&                     create,
        VkSwapchainCreateInfoKHR& create_info
    )l;

private:
    [[nodiscard]] auto update_swapchain() -> bool;

    void fail();
    void choose_surface_format();
    void choose_present_mode  ();
    [[nodiscard]] auto get_surface_format_score(VkSurfaceFormatKHR surface_format) const -> float;
    [[nodiscard]] auto get_present_mode_score(
        VkPresentModeKHR                        present_mode,
        VkSurfacePresentScalingCapabilitiesKHR& out_scaling_capabilities
    ) const -> float;

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

    VkSurfaceCapabilities2KHR              m_surface_capabilities2;
    VkSurfacePresentScalingCapabilitiesKHR m_scaling_capabilities;

    uint32_t m_swapchain_creation_count{0};
};

} // namespace erhe::graphics

