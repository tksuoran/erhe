#pragma once

#if defined(ERHE_GRAPHICS_API_VULKAN)

#include "volk.h"

#include <functional>

namespace erhe::graphics {

// Hooks that let an external subsystem (typically erhe::xr via XR_KHR_vulkan_enable2)
// wrap Vulkan instance / physical device / device creation so that the OpenXR runtime
// can inject the Vulkan extensions and features the HMD needs. If a hook is left empty
// the Vulkan backend falls back to the direct vkCreateInstance / vkEnumeratePhysicalDevices /
// vkCreateDevice path.
class Vulkan_external_creators
{
public:
    // Called instead of vkCreateInstance. The graphics backend passes its own fully
    // populated VkInstanceCreateInfo (layers + extensions already merged). The hook may
    // add more runtime-required extensions on top before forwarding to
    // xrCreateVulkanInstanceKHR.
    std::function<VkResult(const VkInstanceCreateInfo*, VkInstance*)>                      create_instance;

    // Called instead of picking from vkEnumeratePhysicalDevices. Returns the physical
    // device that drives the HMD, as reported by xrGetVulkanGraphicsDevice2KHR.
    std::function<VkResult(VkInstance, VkPhysicalDevice*)>                                 pick_physical_device;

    // Called instead of vkCreateDevice. The graphics backend passes its own fully
    // populated VkDeviceCreateInfo (queue families, extensions, features). The hook may
    // add more runtime-required extensions before forwarding to xrCreateVulkanDeviceKHR.
    std::function<VkResult(VkPhysicalDevice, const VkDeviceCreateInfo*, VkDevice*)>        create_device;
};

} // namespace erhe::graphics

#endif
