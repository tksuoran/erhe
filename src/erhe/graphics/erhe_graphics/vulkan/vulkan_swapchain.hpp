#pragma once

#include "erhe_graphics/swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"

#include <array>
#include <deque>
#include <limits>
#include <vector>

namespace erhe::graphics {

class Device_impl;
class Surface_impl;

enum class Swapchain_frame_state : unsigned int {
    idle                  = 0,
    command_buffer_ready  = 1,  // after wait_frame()
    image_ackquired       = 2,  // after begin_frame()
    render_pass_begin     = 3,  // after begin_render_pass()
    render_pass_end       = 4,  // after end_render_pass()
    command_buffer_submit = 5,  // after submit_command_buffer()
    image_present         = 6   // after end_frame()
};

class Swapchain_objects
{
public:
    std::vector<VkImage>       images;
    std::vector<VkImageView>   views;
    std::vector<VkRenderPass>  render_pass;
    std::vector<VkFramebuffer> framebuffers;
};

class Swapchain_frame_in_flight // PerFrame 
{
public:
    VkFence                        submit_fence     {VK_NULL_HANDLE};
    VkCommandPool                  command_pool     {VK_NULL_HANDLE};
    VkCommandBuffer                command_buffer   {VK_NULL_HANDLE};
    VkSemaphore                    acquire_semaphore{VK_NULL_HANDLE};
    VkSemaphore                    present_semaphore{VK_NULL_HANDLE};

    // Garbage to clean up once the submit_fence is signaled, if any.
    std::vector<Swapchain_objects> swapchain_garbage;
};

class Swapchain_cleanup_data // SwapchainCleanupData
{
public:
    VkSwapchainKHR           swapchain{VK_NULL_HANDLE};  // The old swapchain to be destroyed.

    // Any present semaphores that were pending recycle at the time the swapchain
    // was recreated will be scheduled for recycling at the same time as the swapchain's
    // destruction.
	std::vector<VkSemaphore> semaphores;
};

// Keeps track of resources that are waiting for conditions suitable cleanup
//  - fence and semaphore will be recycled
//  - swapchain(s) will be destroyed
class Present_history_entry // PresentOperationInfo
{
public:
    // Fence that tells when the present semaphore can be destroyed. Without
    // VK_EXT_swapchain_maintenance1, the fence used with the vkAcquireNextImageKHR that
    // returns the same image index in the future is used to know when the semaphore can
    // be recycled.
    VkFence                             cleanup_fence    {VK_NULL_HANDLE};
    VkSemaphore                         present_semaphore{VK_NULL_HANDLE};

    // Old swapchains are scheduled to be destroyed at the same time as the last
    // wait semaphore used to present an image to the old swapchains can be recycled.
	std::vector<Swapchain_cleanup_data> old_swapchains;

    // Used to associate an acquire fence with the previous present operation of
    // the image. Only relevant when VK_EXT_swapchain_maintenance1 is not supported;
    // otherwise a fence is always associated with the present operation.
    uint32_t                            image_index{std::numeric_limits<uint32_t>::max()};
};

class Vulkan_swapchain_create_info;
class Render_pass_impl;

class Swapchain_impl final
{
public:
    Swapchain_impl(Device_impl& device_impl, Surface_impl& surface_impl);
    ~Swapchain_impl() noexcept;

    [[nodiscard]] auto wait_frame           (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame          (const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto begin_render_pass    (VkRenderPassBeginInfo& render_pass_begin_info) -> VkCommandBuffer;
    [[nodiscard]] auto end_render_pass      () -> bool;
    [[nodiscard]] auto submit_command_buffer() -> bool;
    [[nodiscard]] auto end_frame            (const Frame_end_info& frame_end_info) -> bool;
    [[nodiscard]] auto get_surface_impl     () -> Surface_impl&;
    [[nodiscard]] auto get_command_buffer   () -> VkCommandBuffer;
    [[nodiscard]] auto has_depth            () const -> bool;
    [[nodiscard]] auto has_stencil          () const -> bool;

private:
    static constexpr uint32_t INVALID_IMAGE_INDEX = std::numeric_limits<uint32_t>::max();

    void setup_frame();
    [[nodiscard]] auto acquire_next_image(uint32_t* index) -> VkResult;
    [[nodiscard]] auto present_image     (uint32_t index) -> VkResult;
    [[nodiscard]] auto has_maintenance1  () const -> bool;
    [[nodiscard]] auto is_valid          () const -> bool;
    [[nodiscard]] auto get_semaphore     () -> VkSemaphore;
    [[nodiscard]] auto get_fence         () -> VkFence;
    void recycle_semaphore                     (VkSemaphore semaphore);
    void recycle_fence                         (VkFence fence);
    void cleanup_swapchain_objects             (Swapchain_objects& garbage);
    void cleanup_present_history               ();
    void cleanup_present_info                  (Present_history_entry& entry);
    void cleanup_old_swapchain                 (Swapchain_cleanup_data& old_swapchain);
    void init_swapchain                        ();
    void init_swapchain                        (Vulkan_swapchain_create_info& swapchain_create_info);
    void init_swapchain_image                  (uint32_t index);
    void init_swapchain_framebuffer            (uint32_t index, VkRenderPass render_pass);
    void add_present_to_history                (uint32_t index, VkFence present_fence);
    void associate_fence_with_present_history  (uint32_t index, VkFence acquire_fence);
    void schedule_old_swapchain_for_destruction(VkSwapchainKHR old_swapchain);

    Device_impl&          m_device_impl;
    Surface_impl&         m_surface_impl;
    VkExtent2D            m_swapchain_extent    {0, 0};
    VkFormat              m_swapchain_format    {VK_FORMAT_UNDEFINED};
    bool                  m_is_valid            {false};
    uint32_t              m_acquired_image_index{0};
    VkSwapchainKHR        m_vulkan_swapchain    {VK_NULL_HANDLE};
    //VkRenderPass          m_vulkan_render_pass  {VK_NULL_HANDLE};
    Swapchain_objects     m_swapchain_objects;

    Swapchain_frame_state m_state{Swapchain_frame_state::idle};
    // TODO Move to Render_pass_impl

    /// The submission history. This is a fixed-size queue, implemented as a circular buffer.
    std::array<Swapchain_frame_in_flight, 2> m_submit_history      {};
    size_t                                   m_submit_history_index{0};

    /// The present operation history. This is used to clean up present semaphores and old swapchains.
    std::deque<Present_history_entry>        m_present_history;

     // The previous swapchain which needs to be scheduled for destruction when
     // appropriate. This will be done when the first image of the current swapchain is
     // presented. If there were older swapchains pending destruction when the swapchain is
     // recreated, they will accumulate and be destroyed with the previous swapchain.
     //
     // Note that if the user resizes the window such that the swapchain is recreated every
     // frame, this array can go grow indefinitely.
    std::vector<Swapchain_cleanup_data>      m_old_swapchains;

    /// Resource pools.
    std::vector<VkSemaphore>                 m_semaphore_pool;
    std::vector<VkFence>                     m_fence_pool;
    size_t                                   m_semaphore_serial{0};
    size_t                                   m_fence_serial    {0};
    size_t                                   m_swapchain_serial{0};
};

} // namespace erhe::graphics
