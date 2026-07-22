#pragma once

#include "erhe_graphics/swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

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
    VkImage                    depth_image     {VK_NULL_HANDLE};
    VkImageView                depth_view      {VK_NULL_HANDLE};
    VmaAllocation              depth_allocation{VK_NULL_HANDLE};
};

class Swapchain_frame_in_flight // PerFrame
{
public:
    VkSemaphore                    acquire_semaphore{VK_NULL_HANDLE};
    VkSemaphore                    present_semaphore{VK_NULL_HANDLE};

    // True while acquire_semaphore holds a handle that was taken from the
    // sync pool and has not yet been returned. The handle stays valid in
    // the field after its submission consumes it (safe to recycle on the
    // next visit to this slot) and across intervening device-only submits
    // that don't touch swapchain state. Device-only slots (init-time
    // priming, or OpenXR ticks with the mirror window off) never allocate
    // an acquire_semaphore, so the flag stays false for them and
    // setup_frame knows to skip the recycle.
    bool                           acquire_semaphore_pending_recycle{false};

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
	std::vector<VkSemaphore> semaphores{};
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

    // Drop the VkSwapchainKHR + per-image / per-frame Vulkan handles, drain
    // the GPU and recycle synchronization primitives, but leave this
    // Swapchain_impl object alive in a state equivalent to a freshly
    // constructed one. Used by Surface_impl::recreate_for_new_window when
    // the underlying VkSurfaceKHR is being replaced: the C++ Swapchain
    // identity stays stable, so cached raw Swapchain* pointers in
    // Render_pass_impl etc. remain valid. The next wait_frame() rebuilds
    // the VkSwapchainKHR against the new VkSurfaceKHR.
    void reset_for_new_surface();

    [[nodiscard]] auto wait_frame           (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame          (const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto begin_render_pass    (VkRenderPassBeginInfo& render_pass_begin_info) -> VkCommandBuffer;
    [[nodiscard]] auto end_render_pass      () -> bool;
    [[nodiscard]] auto submit_command_buffer() -> bool;
    [[nodiscard]] auto end_frame            (const Frame_end_info& frame_end_info) -> bool;
    // Used by Render_pass_impl::start_render_pass to fill in the
    // framebuffer + render area for a swapchain-targeted render pass
    // recorded into the caller's Command_buffer (no longer routed
    // through the legacy Swapchain_impl::begin_render_pass cb).
    // Lazy-creates the per-image framebuffer if it doesn't exist or
    // was created against a different VkRenderPass.
    [[nodiscard]] auto acquire_swapchain_framebuffer(VkRenderPass render_pass) -> VkFramebuffer;
    [[nodiscard]] auto get_swapchain_extent() const -> VkExtent2D;
    void               mark_render_pass_recorded();
    // Drive vkQueuePresentKHR for the currently acquired image. Caller
    // must have already submitted a cb that signals this swapchain's
    // present_semaphore (the implicit-present path: Device_impl::submit_command_buffers
    // calls this after vkQueueSubmit2 returns). Handles VK_SUBOPTIMAL_KHR
    // / VK_ERROR_OUT_OF_DATE_KHR by triggering swapchain recreation.
    [[nodiscard]] auto present              () -> bool;
    [[nodiscard]] auto get_surface_impl     () -> Surface_impl&;
    [[nodiscard]] auto get_command_buffer   () -> VkCommandBuffer;
    // Active per-frame-in-flight semaphores. acquire is signalled by
    // vkAcquireNextImageKHR (must be waited before the cb writes the
    // swapchain image); present is signalled by the cb's submit (must
    // be waited by vkQueuePresentKHR). Both are valid only between
    // setup_frame()/begin_frame() and the present in end_frame().
    [[nodiscard]] auto get_active_acquire_semaphore() const -> VkSemaphore;
    [[nodiscard]] auto get_active_present_semaphore() const -> VkSemaphore;
    // Present-wait clamp (frame pacing FR5, implementation plan step P2.2):
    // block until the present with id (frame_id + 1), as chained by
    // present_image(), has been displayed. Only ids already submitted on
    // the CURRENT VkSwapchainKHR are waited on -- right after recreation
    // the old swapchain's ids can no longer be waited, so unsupported is
    // returned until this swapchain has presented past the requested id.
    [[nodiscard]] auto wait_for_displayed_frame(std::int64_t frame_id, uint64_t timeout_ns) -> Present_wait_result;
    [[nodiscard]] auto has_depth            () const -> bool;
    [[nodiscard]] auto has_stencil          () const -> bool;
    [[nodiscard]] auto get_color_format     () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format     () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_image_view () const -> VkImageView;
    [[nodiscard]] auto get_vk_depth_format  () const -> VkFormat;

private:
    static constexpr uint32_t INVALID_IMAGE_INDEX = std::numeric_limits<uint32_t>::max();

    // Body of the destructor / reset_for_new_surface(): drain the GPU,
    // recycle per-frame semaphores and fences, free swapchain garbage,
    // drain present history, destroy any pending old swapchains, and
    // finally destroy m_vulkan_swapchain. Leaves m_vulkan_swapchain at
    // VK_NULL_HANDLE on return; does NOT reset the higher-level state
    // fields (extent / format / m_is_valid / m_state) because the
    // destructor does not need to.
    void release_resources();

    void setup_frame();
    [[nodiscard]] auto acquire_next_image(uint32_t* index) -> VkResult;
    [[nodiscard]] auto present_image     (uint32_t index) -> VkResult;
    [[nodiscard]] auto has_maintenance1  () const -> bool;
    [[nodiscard]] auto is_valid          () const -> bool;
    [[nodiscard]] auto get_semaphore     () -> VkSemaphore;
    [[nodiscard]] auto get_fence         () -> VkFence;
    void recycle_semaphore                     (VkSemaphore semaphore);
    [[nodiscard]] auto current_slot() const -> size_t;
    void recycle_fence                         (VkFence fence);
    void cleanup_swapchain_objects             (Swapchain_objects& garbage);
    void cleanup_present_history               ();
    void cleanup_present_info                  (Present_history_entry& entry);
    void cleanup_old_swapchain                 (Swapchain_cleanup_data& old_swapchain);
    void init_swapchain                        ();
    void init_swapchain                        (Vulkan_swapchain_create_info& swapchain_create_info);
    void init_swapchain_image                  (uint32_t index);
    void init_swapchain_framebuffer            (uint32_t index, VkRenderPass render_pass);
    void create_depth_image                    (uint32_t width, uint32_t height);
    void destroy_depth_image                   (Swapchain_objects& objects);
    void add_present_to_history                (uint32_t index, VkFence present_fence);
    void associate_fence_with_present_history  (uint32_t index, VkFence acquire_fence);
    void schedule_old_swapchain_for_destruction(VkSwapchainKHR old_swapchain);
    // Present timing (implementation plan step P0.4): opt in to
    // VK_EXT_present_timing on this swapchain, query refresh duration and
    // time domains, and pick the present stage to query. Called after every
    // (re)creation so the refresh period is re-queried per swapchain.
    void init_present_timing                   (const Vulkan_swapchain_create_info& swapchain_create_info);
    // Drains available past-presentation timings into the device's frame
    // time recorder (achieved present time per frame id).
    void poll_present_timing                   ();
    // Calibrates the swapchain's present-stage-local time domain against the
    // host clock (VkSwapchainCalibratedTimestampInfoEXT chained into
    // vkGetCalibratedTimestamps). No-op for host time domains.
    void update_present_timing_calibration     ();

    Device_impl&          m_device_impl;
    Surface_impl&         m_surface_impl;
    VkExtent2D            m_swapchain_extent    {0, 0};
    VkFormat              m_swapchain_format    {VK_FORMAT_UNDEFINED};
    VkFormat              m_depth_format        {VK_FORMAT_UNDEFINED};
    bool                  m_is_valid            {false};
    uint32_t              m_acquired_image_index{0};
    VkSwapchainKHR        m_vulkan_swapchain    {VK_NULL_HANDLE};
    //VkRenderPass          m_vulkan_render_pass  {VK_NULL_HANDLE};
    Swapchain_objects     m_swapchain_objects;

    Swapchain_frame_state m_state{Swapchain_frame_state::idle};
    // TODO Move to Render_pass_impl

    /// The submission history. This is a fixed-size queue, indexed by the
    /// device's frame-in-flight slot (Device_impl::get_frame_in_flight_index).
    /// Swapchain_impl does not own a separate counter -- having two
    /// independent counters caused the swapchain and device to disagree on
    /// the current slot whenever an end_frame happened without a matching
    /// setup_frame (init-time prime, OpenXR-only ticks). All slot lookups
    /// go through current_slot() below.
    std::array<Swapchain_frame_in_flight, 2> m_submit_history{};

    /// The present operation history. This is used to clean up present semaphores and old swapchains.
    std::deque<Present_history_entry>        m_present_history;

    // Present timing state (implementation plan step P0.4). Requires the
    // swapchain to be created with VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT
    // (vulkan_surface.cpp) - presenting timing requests against a swapchain
    // without that flag crashes inside the driver.
    bool                   m_present_timing_active  {false};
    uint64_t               m_timing_time_domain_id  {0};
    VkTimeDomainKHR        m_timing_time_domain     {VK_TIME_DOMAIN_DEVICE_KHR};
    VkPresentStageFlagsEXT m_present_stage_queries  {0};
    // All surface-supported stages, requested per present so feedback
    // carries every stage; m_present_stage_queries stays the PRIMARY
    // (scanout-near) stage used for achieved_present_time, calibration and
    // the pacer grid. QUEUE_OPERATIONS_END feedback (when supported) lands
    // in achieved_queue_ops_time as a queue-vs-scanout diagnostic.
    VkPresentStageFlagsEXT m_present_stage_queries_mask{0};
    uint64_t               m_refresh_duration_ns    {0};
    bool                   m_present_feedback_logged{false};
    // FR3 target-time request modes (step P2.3): device feature AND surface
    // capability. The development machine reports absolute unsupported on
    // the surface while the device feature is TRUE - the relative path is
    // the one actually exercised there.
    bool                   m_present_at_absolute_time{false};
    bool                   m_present_at_relative_time{false};
    bool                   m_timing_calibration_valid       {false};
    uint64_t               m_timing_calibration_domain_value{0};
    double                 m_timing_calibration_host_seconds{0.0};
    uint64_t               m_timing_calibration_frame       {0};
    // Per-stage clocks: the stage-local domain's epoch differs per stage,
    // so QUEUE_OPERATIONS_END feedback needs its own calibration pair.
    bool                   m_timing_calibration_queue_ops_valid{false};
    uint64_t               m_timing_calibration_queue_ops_value{0};
    std::array<VkPastPresentationTimingEXT, 8>          m_past_timings{};
    std::array<std::array<VkPresentStageTimeEXT, 4>, 8> m_past_timing_stages{};

    // Highest present id actually submitted (vkQueuePresentKHR succeeded)
    // on m_vulkan_swapchain. Reset when a new VkSwapchainKHR is created:
    // present ids are per swapchain object, so waiting on an id from a
    // previous swapchain would only ever time out. Guards
    // wait_for_displayed_frame (step P2.2).
    uint64_t               m_max_present_id_submitted{0};

     // The previous swapchain which needs to be scheduled for destruction when
     // appropriate. This will be done when the first image of the current swapchain is
     // presented. If there were older swapchains pending destruction when the swapchain is
     // recreated, they will accumulate and be destroyed with the previous swapchain.
     //
     // Note that if the user resizes the window such that the swapchain is recreated every
     // frame, this array can go grow indefinitely.
    std::vector<Swapchain_cleanup_data>      m_old_swapchains;

    size_t                                   m_swapchain_serial{0};
};

} // namespace erhe::graphics
