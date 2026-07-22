#pragma once

#include "erhe_graphics/surface.hpp"

#include "volk.h"

#include <cstdint>
#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Device_impl;
class Emulated_swapchain_impl;
class Surface_create_info;
class Swapchain;
class Vulkan_swapchain_create_info;

class Vulkan_swapchain_create_info
{
public:
    VkSwapchainCreateInfoKHR               swapchain_create_info;
    VkSwapchainPresentScalingCreateInfoKHR swapchain_present_scaling_create_info;
    VkSwapchainPresentModesCreateInfoKHR   swapchain_present_modes_create_info;
    std::vector<VkPresentModeKHR>          present_modes;
    // Exclusive fullscreen (VK_EXT_full_screen_exclusive, application
    // controlled). When use_full_screen_exclusive is set the two structs
    // below are chained into swapchain_create_info.pNext and the caller
    // (Swapchain_impl::init_swapchain) must acquire the exclusive mode
    // after creating the swapchain.
    bool                                   use_full_screen_exclusive{false};
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    VkSurfaceFullScreenExclusiveInfoEXT      full_screen_exclusive_info{};
    VkSurfaceFullScreenExclusiveWin32InfoEXT full_screen_exclusive_win32_info{};
#endif
};

class Surface_impl final
{
public:
    Surface_impl(Device_impl& device, const Surface_create_info& create_info, bool headless);
    ~Surface_impl() noexcept;

    [[nodiscard]] auto is_empty() -> bool;
    [[nodiscard]] auto is_valid() -> bool;
    [[nodiscard]] auto is_headless           () const -> bool;
    [[nodiscard]] auto get_swapchain          () -> Swapchain*;
    [[nodiscard]] auto get_emulated_swapchain () const -> Emulated_swapchain_impl*;
    [[nodiscard]] auto get_color_format       () -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format       () -> erhe::dataformat::Format;
    [[nodiscard]] auto can_use_physical_device(VkPhysicalDevice physical_device) -> bool;
    [[nodiscard]] auto use_physical_device    (VkPhysicalDevice physical_device) -> bool;

    // Tear the existing VkSwapchainKHR + VkSurfaceKHR down and rebuild
    // over the current Context_window's freshly-bound native window.
    // The owning Swapchain (and its Swapchain_impl) C++ object stays
    // alive: only the Vulkan handles inside it are dropped, so cached
    // raw Swapchain* pointers (e.g. in Render_pass_impl, in
    // Window_imgui_host::m_render_pass) remain valid across the call.
    // The next Swapchain_impl::wait_frame() rebuilds the
    // VkSwapchainKHR against the new VkSurfaceKHR.
    //
    // Used on Android when DID_ENTER_FOREGROUND delivers a new
    // ANativeWindow and the previous VkSurfaceKHR has been invalidated
    // (the editor observed VK_ERROR_SURFACE_LOST_KHR or
    // m_swapchain_dirty was set by the lifecycle event watch).
    [[nodiscard]] auto recreate_for_new_window() -> bool;
    [[nodiscard]] auto get_surface_format     () const -> VkSurfaceFormatKHR;
    [[nodiscard]] auto get_present_mode       () const -> VkPresentModeKHR;
    [[nodiscard]] auto get_image_count        () const -> uint32_t;
    [[nodiscard]] auto get_vulkan_surface     () const -> VkSurfaceKHR;

    // Returns true if swapchain needs to be (re)created
    [[nodiscard]] auto update_swapchain(Vulkan_swapchain_create_info& out_swapchain_create_info) -> bool;

    [[nodiscard]] auto get_surface_create_info() const -> const Surface_create_info&;

    // Presentation pre-rotation the swapchain was last (re)created with, mapped
    // to the backend-agnostic enum. See update_swapchain().
    [[nodiscard]] auto get_surface_transform() const -> Surface_transform;

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
    // currentTransform the live swapchain was created with. Used both to detect
    // a rotation change that needs a swapchain recreate (90 <-> 270 keeps the
    // native extent but flips the transform) and to report the pre-rotation the
    // renderer must apply.
    VkSurfaceTransformFlagBitsKHR   m_swapchain_transform{VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR};
    VkSwapchainKHR                  m_vulkan_swapchain{VK_NULL_HANDLE};
    std::unique_ptr<Swapchain>               m_swapchain;
    std::unique_ptr<Emulated_swapchain_impl> m_emulated_swapchain;
    bool                            m_headless{false};
    bool                            m_is_empty{true};
    bool                            m_is_valid{false};
};

} // namespace erhe::graphics

