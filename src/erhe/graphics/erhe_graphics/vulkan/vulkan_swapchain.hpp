#pragma once

#include "erhe_graphics/swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"

#include <array>
#include <deque>
#include <vector>

namespace erhe::graphics {

class Device_impl;
class Surface_impl;

class Swapchain_objects 
{
public:
    std::vector<VkImage>       images;
    std::vector<VkImageView>   views;
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
    VkSwapchainKHR          swapchain{VK_NULL_HANDLE};  // The old swapchain to be destroyed.

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

class Swapchain_impl final
{
public:
    Swapchain_impl(
        Device_impl&              device_impl,
        Surface_impl&             surface_impl,
        VkSwapchainCreateInfoKHR& vulkan_swapchain_create_info
    );
    ~Swapchain_impl() noexcept;

    void start_of_frame  ();
    void present         ();

    void update_vulkan_swapchain(VkSwapchainCreateInfoKHR& swapchain_create_info);

    [[nodiscard]] auto get_image_count() const -> size_t;
    [[nodiscard]] auto get_image_entry(size_t image_index) -> Swapchain_image_entry&;
    [[nodiscard]] auto get_image_entry(size_t image_index) const -> const Swapchain_image_entry&;

    [[nodiscard]] auto get_command_buffer() -> VkCommandBuffer;

private:
    static constexpr uint32_t INVALID_IMAGE_INDEX = std::numeric_limits<uint32_t>::max();

	[[nodiscard]] auto get_semaphore() -> VkSemaphore;
	[[nodiscard]] auto get_fence    () -> VkFence;
	void recycle_semaphore(VkSemaphore semaphore);
	void recycle_fence    (VkFence fence);

    void cleanup_swapchain_objects(Swapchain_objects& garbage);
    void cleanup_present_history  ();
    void cleanup_present_info     (Present_history_entry& entry);
    void cleanup_old_swapchain    (Swapchain_cleanup_data& old_swapchain);

    void init_swapchain_image(uint32_t index);

    void add_present_to_history(uint32_t index, VkFence present_fence);
    void associate_fence_with_present_history(uint32_t index, VkFence acquire_fence);
    void schedule_old_swapchain_for_destruction(VkSwapchainKHR old_swapchain);


    //void create_frames_in_flight_resources();
    //void create_image_entry_resources();

    // placeholder
    void create_placeholder_renderpass_and_framebuffers();
    void submit_placeholder_renderpass();

    Device_impl&       m_device_impl;
    Surface_impl&      m_surface_impl;
    VkSwapchainKHR     m_vulkan_swapchain{VK_NULL_HANDLE};
    VkExtent2D         m_extent;
    //// std::vector<Swapchain_frame_in_flight> m_frames_in_flight;
    //// std::vector<Swapchain_image_entry>     m_image_entries;
    //// VkRenderPass                           m_vulkan_renderpass{VK_NULL_HANDLE}; // placeholder
    //// bool                                   m_is_valid         {true};

    /// The swapchain.
    VkSwapchainKHR    m_vulkan_swapchain{VK_NULL_HANDLE};

    /// Swapchain data.
    VkPresentModeKHR  m_current_present_mode{VK_PRESENT_MODE_FIFO_KHR};
    VkPresentModeKHR  m_desired_present_mode{VK_PRESENT_MODE_FIFO_KHR};
    Swapchain_objects m_swapchain_objects;

    /// The render pass used for rendering.
    VkRenderPass      m_render_pass{VK_NULL_HANDLE};

    /// The submission history. This is a fixed-size queue, implemented as a circular buffer.
    std::array<Swapchain_frame_in_flight, 2> m_submit_history      {};
    size_t                                   m_submit_history_index{0};

    /// The present operation history.  This is used to clean up present semaphores and old swapchains.
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
    std::vector<VkSemaphore> m_semaphore_pool;
    std::vector<VkFence>     m_fence_pool;

    // Other statistics

    // User toggles.
    bool m_recreate_swapchain_on_present_mode_change = false;

};

} // namespace erhe::graphics

// fence and semaphore lifetime / ownership is outside frames in flight,
// instead, there is a pool of fences and pool of semaphores;
// they can be recycled, but logic is more complex than after N frames
// (where N is number of frames in flight)
// This is because safely recycling these resources (fences and semaphores)
// requires we must ensure presentation is no longer using these resources.
// When swapchain is being recreated (for example due to window resize),
// getting this right is not simple

// frame submit_fence:
//  - recycled in SwapchainRecreation::setup_frame()
//  - allocated in SwapchainRecreation::setup_frame()
//  - signaled via vkQueueSubmit()
//  - waited by SwapchainRecreation::setup_frame()
// frame acquire_semaphore:
//  - recycled in SwapchainRecreation::setup_frame()
//  - allocated in SwapchainRecreation::setup_frame()
//  - signaled via vkAcquireNextImageKHR in SwapchainRecreation::acquire_next_image()
//  - waited by vkQueueSubmit() in SwapchainRecreation::render()
// frame present_semaphore:
//  - allocated in SwapchainRecreation::setup_frame()
//  - signaled via vkQueueSubmit() in SwapchainRecreation::render()
//  - waited by vkQueuePresentKHR() in SwapchainRecreation::present_image()

// present history present_fence aka cleanup_fence
//  - this is not frame in flight thing
//  - signaled via vkQueuePresentKHR() in SwapchainRecreation::present_image()

// SwapchainRecreation::old_swapchains keeps track of swapchains that must be later destroyed,
// but first we need to wait for something newer to be presented
// - Entries are added in schedule_old_swapchain_for_destruction() - these don't yet have associated present
// - Entries are moved to present history in add_present_to_history() - entries here are associated with present
// - cleanup_present_history() is where present history is scanned for entries that can be removed
// - cleanup_present_info() recycles fence and semaphore, and destroys old swapchain