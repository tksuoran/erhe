#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_device_sync_pool.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#if defined(ERHE_OS_MACOS)
#   include "erhe_graphics/vulkan/vulkan_metal_layer_mac.hpp"
#endif
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdlib>

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(
    Device_impl&  device_impl,
    Surface_impl& surface_impl
)
    : m_device_impl {device_impl}
    , m_surface_impl{surface_impl}
{
    // Note: Device_sync_pool is NOT captured here -- Surface_impl (and thus
    // Swapchain_impl) is constructed early in Device_impl init to query
    // physical-device presentation support, before the Device_impl body
    // creates m_sync_pool. The delegating methods below fetch the pool
    // on demand from m_device_impl.
}

auto Swapchain_impl::current_slot() const -> size_t
{
    // Single source of truth for "which frame-in-flight slot is the
    // current one". Defined by the device's frame index modulo N. The
    // device advances this exactly once per Device_impl::end_frame; reads
    // are stable for the duration of a single frame
    // (wait_frame -> begin_frame -> ... -> end_frame).
    return m_device_impl.get_frame_in_flight_index();
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    log_swapchain->debug("Swapchain_impl::~Swapchain_impl()");
    release_resources();
}

void Swapchain_impl::release_resources()
{
    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        return;
    }

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result = VK_SUCCESS;

    // TODO Queue thread safety
    result = vkDeviceWaitIdle(vulkan_device);
    if (result != VK_SUCCESS) {
        m_device_impl.get_device().device_message(
            Message_severity::error,
            fmt::format("vkDeviceWaitIdle() failed with {} {}", static_cast<int32_t>(result), c_str(result))
        );
    }

    for (size_t i = 0; i < m_submit_history.size(); ++i) {
        Device_frame_in_flight& device_frame = m_device_impl.get_device_frame_in_flight(i);
        recycle_fence(device_frame.submit_fence);
        device_frame.submit_fence = VK_NULL_HANDLE;
    }
    for (Swapchain_frame_in_flight& frame : m_submit_history) {
        if (frame.acquire_semaphore_pending_recycle) {
            recycle_semaphore(frame.acquire_semaphore);
            frame.acquire_semaphore                = VK_NULL_HANDLE;
            frame.acquire_semaphore_pending_recycle = false;
        }
        for (Swapchain_objects& garbage : frame.swapchain_garbage) {
            cleanup_swapchain_objects(garbage);
        }
        frame.swapchain_garbage.clear();
        // Normally present_semaphore is moved into the present-history
        // entry by add_present_to_history(), and the assert below would
        // hold. The SURFACE_LOST / DEVICE_LOST early return in
        // present_image() skips that step (no history entry is created
        // since the present did not happen), leaving the semaphore on
        // the frame slot. The vkDeviceWaitIdle above guarantees the GPU
        // is no longer signaling or waiting on it, so recycle it here
        // instead of asserting.
        if (frame.present_semaphore != VK_NULL_HANDLE) {
            recycle_semaphore(frame.present_semaphore);
            frame.present_semaphore = VK_NULL_HANDLE;
        }
    }

    for (Present_history_entry& present_history_entry : m_present_history) {
        if (present_history_entry.cleanup_fence != VK_NULL_HANDLE) {
            result = vkWaitForFences(vulkan_device, 1, &present_history_entry.cleanup_fence, true, UINT64_MAX);
            if (result != VK_SUCCESS) {
                m_device_impl.get_device().device_message(
                    Message_severity::error,
                    fmt::format("vkWaitForFences() failed with {} {}", static_cast<int32_t>(result), c_str(result))
                );
            }
        }
        cleanup_present_info(present_history_entry);
    }
    m_present_history.clear();

    //log_swapchain->trace("During the lifetime, {} swapchains were created", m_swapchain_creation_count);
    log_swapchain->debug("Old swapchain count at release: {}", m_old_swapchains.size());

    for (Swapchain_cleanup_data& old_swapchain : m_old_swapchains) {
        cleanup_old_swapchain(old_swapchain);
    }
    m_old_swapchains.clear();

    cleanup_swapchain_objects(m_swapchain_objects);
    if (m_vulkan_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkan_device, m_vulkan_swapchain, nullptr);
        m_vulkan_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain_impl::reset_for_new_surface()
{
    log_swapchain->debug("Swapchain_impl::reset_for_new_surface()");

    // Drop every Vulkan handle owned by this Swapchain_impl. After this
    // call m_vulkan_swapchain is VK_NULL_HANDLE and the per-frame slots
    // hold no live semaphores or framebuffers; the next wait_frame()
    // will see m_vulkan_swapchain == VK_NULL_HANDLE and run
    // init_swapchain() against the (now updated) VkSurfaceKHR in
    // Surface_impl.
    release_resources();

    // Restore higher-level state to the post-construction defaults so
    // wait_frame()'s state precondition holds and init_swapchain() does
    // not see a cached extent that would early-out the rebuild. The
    // device frame-in-flight index is owned by Device_impl and keeps
    // advancing across the reset; current_slot() therefore stays
    // consistent with the device timeline.
    m_swapchain_extent     = VkExtent2D{0, 0};
    m_swapchain_format     = VK_FORMAT_UNDEFINED;
    m_depth_format         = VK_FORMAT_UNDEFINED;
    m_is_valid             = false;
    m_acquired_image_index = 0;
    m_state                = Swapchain_frame_state::idle;
}

auto Swapchain_impl::submit_command_buffer() -> bool
{
    if (!is_valid()) {
        return false;
    }

    ERHE_VERIFY(m_state == Swapchain_frame_state::render_pass_end);

    const Swapchain_frame_in_flight& frame        = m_submit_history[current_slot()];
    const Device_frame_in_flight&    device_frame = m_device_impl.get_device_frame_in_flight(current_slot());
    VkQueue  vulkan_graphics_queue = m_device_impl.get_graphics_queue();
    VkResult result                = VK_SUCCESS;

    // Make a submission. Wait on the acquire semaphore and signal the present semaphore.
    const VkSemaphoreSubmitInfo wait_semaphore_info{
        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext       = nullptr,
        .semaphore   = frame.acquire_semaphore,
        .value       = 0,
        .stageMask   = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .deviceIndex = 0,
    };
    const VkSemaphoreSubmitInfo signal_semaphore_info{
        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext       = nullptr,
        .semaphore   = frame.present_semaphore,
        .value       = 0,
        .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .deviceIndex = 0,
    };
    const VkCommandBufferSubmitInfo command_buffer_info{
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext         = nullptr,
        .commandBuffer = device_frame.command_buffer,
        .deviceMask    = 0,
    };
    const VkSubmitInfo2 submit_info{
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext                    = nullptr,
        .flags                    = 0,
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &wait_semaphore_info,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &command_buffer_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal_semaphore_info,
    };
    log_swapchain->trace("vkQueueSubmit2()");
    result = vkQueueSubmit2(vulkan_graphics_queue, 1, &submit_info, device_frame.submit_fence);
    if (result != VK_SUCCESS) {
        m_device_impl.get_device().device_message(
            Message_severity::error,
            fmt::format("vkQueueSubmit2() failed with {} {}", static_cast<int32_t>(result), c_str(result))
        );
    }

    m_state = Swapchain_frame_state::command_buffer_submit;
    return true;
}

auto Swapchain_impl::wait_frame(Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(
        (m_state == Swapchain_frame_state::idle) ||
        (m_state == Swapchain_frame_state::image_present)
    );

    out_frame_state = Frame_state{};

    // (Re)build the swapchain when we have no handle, or the previous
    // present / acquire marked it invalid (e.g. SURFACE_LOST after the
    // OS detached our window during background). init_swapchain will
    // also fail with SURFACE_LOST until the underlying VkSurfaceKHR is
    // recreated by the foreground lifecycle path; in that case we just
    // keep returning false from here, leaving m_state at idle.
    if ((m_vulkan_swapchain == VK_NULL_HANDLE) || !m_is_valid) {
        init_swapchain();
    }

    if (!m_is_valid) {
        // Skip the frame without consuming any per-slot resources.
        // Leaving m_state at idle keeps this method's entry assertion
        // happy on subsequent calls while the swapchain remains down.
        m_state = Swapchain_frame_state::idle;
        out_frame_state.should_render = false;
        return false;
    }

    setup_frame();

    m_state = Swapchain_frame_state::command_buffer_ready;

    return true;
}

auto Swapchain_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    ERHE_VERIFY(m_state == Swapchain_frame_state::command_buffer_ready);

    VkResult result = VK_SUCCESS;

#if defined(ERHE_OS_MACOS)
    // Force CAMetalLayer.drawableSize to match the swapchain extent before
    // every acquire. AppKit's live-resize layout callbacks can reset the
    // layer's drawableSize to (bounds x contentsScale) transiently during
    // fast drag operations -- the bounds go to 0 while the window is being
    // re-laid-out, which CAMetalLayer clamps to a 1x1 drawable. We want
    // MoltenVK's nextDrawable to hand us a drawable that matches the
    // VkImage extent, not whatever AppKit last auto-adjusted to.
    auto pin_drawable_size_to_swapchain_extent = [&]() {
        const erhe::window::Context_window* const ctx_window =
            m_surface_impl.get_surface_create_info().context_window;
        if ((ctx_window == nullptr) || (m_swapchain_extent.width == 0) || (m_swapchain_extent.height == 0)) {
            return;
        }
        set_sdl_window_drawable_size(
            ctx_window->get_sdl_window(),
            static_cast<int>(m_swapchain_extent.width),
            static_cast<int>(m_swapchain_extent.height)
        );
    };
#endif

    if (!frame_begin_info.request_resize) {
#if defined(ERHE_OS_MACOS)
        pin_drawable_size_to_swapchain_extent();
#endif
        result = acquire_next_image(&m_acquired_image_index);
    }

    if (
        frame_begin_info.request_resize ||
        (result == VK_SUBOPTIMAL_KHR) ||
        (result == VK_ERROR_OUT_OF_DATE_KHR) ||
        (result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT) // recreate re-acquires the exclusive mode
    ) {
        init_swapchain();
        if (!m_is_valid) {
            m_acquired_image_index = 0xffu;
            m_state                = Swapchain_frame_state::idle;
            return false;
        }
#if defined(ERHE_OS_MACOS)
        pin_drawable_size_to_swapchain_extent();
#endif
        result = acquire_next_image(&m_acquired_image_index);
    }
    if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
        log_context->warn("Could not obtain swapchain image, skipping frame");
        m_acquired_image_index = 0xffu;
        m_state                = Swapchain_frame_state::idle;
        return false;
    }

    // Allocate present_semaphore now that an image has been acquired.
    // setup_frame no longer does this unconditionally; see its comment.
    Swapchain_frame_in_flight& frame = m_submit_history[current_slot()];
    ERHE_VERIFY(frame.present_semaphore == VK_NULL_HANDLE);
    frame.present_semaphore = get_semaphore();
    m_state = Swapchain_frame_state::image_ackquired;
    ERHE_VULKAN_SYNC_TRACE(
        "[SWAPCHAIN_BEGIN] frame_index={} acquired_image_index={} submit_history_index={}",
        m_device_impl.get_frame_index(), m_acquired_image_index, current_slot()
    );
    return true;
}

// Called by Render_pass_impl::start_render_pass()
auto Swapchain_impl::begin_render_pass(VkRenderPassBeginInfo& render_pass_begin_info) -> VkCommandBuffer
{
    if (!is_valid()) {
        return VK_NULL_HANDLE;
    }

    ERHE_VERIFY(
        (m_state == Swapchain_frame_state::image_ackquired) ||
        (m_state == Swapchain_frame_state::render_pass_end)
    );

    //const Swapchain_frame_in_flight& frame = m_submit_history[current_slot()];
    //VkQueue  vulkan_graphics_queue = m_device_impl.get_graphics_queue();
    uint64_t frame_index           = m_device_impl.get_frame_index();

    log_swapchain->trace(
        "Swapchain_impl::begin_render_pass() slot = {} m_acquired_image_index = {} frame_index = {}",
        current_slot(), m_acquired_image_index, frame_index
    );

    const VkCommandBuffer vulkan_command_buffer = get_command_buffer();
    if (vulkan_command_buffer == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // Note: the command buffer was already vkBeginCommandBuffer'd by
    // Device_impl::begin_frame(). We just record vkCmdBeginRenderPass2 here
    // and let Device_impl::end_frame() end + submit it.

    if (
        (m_swapchain_objects.framebuffers[m_acquired_image_index] == VK_NULL_HANDLE) ||
        (m_swapchain_objects.render_pass [m_acquired_image_index] != render_pass_begin_info.renderPass)
    ) {
        init_swapchain_framebuffer(m_acquired_image_index, render_pass_begin_info.renderPass);
    }

    render_pass_begin_info.framebuffer = m_swapchain_objects.framebuffers[m_acquired_image_index];
    render_pass_begin_info.renderArea  = VkRect2D{{0,0}, m_swapchain_extent};

    const VkSubpassBeginInfo subpass_begin_info{
        .sType    = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
        .pNext    = nullptr,
        .contents = VK_SUBPASS_CONTENTS_INLINE
    };
    log_swapchain->trace("vkCmdBeginRenderPass2()");
    vkCmdBeginRenderPass2(vulkan_command_buffer, &render_pass_begin_info, &subpass_begin_info);

    m_state = Swapchain_frame_state::render_pass_begin;
    return vulkan_command_buffer;
}

auto Swapchain_impl::end_render_pass() -> bool
{
    if (!is_valid()) {
        return false;
    }

    ERHE_VERIFY(m_state == Swapchain_frame_state::render_pass_begin);

    const VkCommandBuffer vulkan_command_buffer = get_command_buffer();
    if (vulkan_command_buffer == VK_NULL_HANDLE) {
        return false;
    }

    const VkSubpassEndInfo subpass_end_info{
        .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
        .pNext = nullptr
    };
    log_swapchain->trace("vkCmdEndRenderPass2()");
    vkCmdEndRenderPass2(vulkan_command_buffer, &subpass_end_info);

    // Note: vkEndCommandBuffer is now done by Device_impl::end_frame(),
    // which ends and submits the unified device-frame command buffer.

    m_state = Swapchain_frame_state::render_pass_end;
    return true;
}

auto Swapchain_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    if (m_state == Swapchain_frame_state::render_pass_end) {
        const bool submit_ok = submit_command_buffer();
        if (!submit_ok) {
            return false;
        }
    }
    ERHE_VERIFY(m_state == Swapchain_frame_state::command_buffer_submit);

    static_cast<void>(frame_end_info);

    return present();
}

auto Swapchain_impl::present() -> bool
{
    // Drive vkQueuePresentKHR for the currently acquired image. Caller
    // is responsible for having submitted a cb that signals this
    // swapchain's present_semaphore. No state assertion: the legacy
    // end_frame path arrives here at command_buffer_submit (after its
    // own submit_command_buffer ran), while the new implicit-present
    // path (Device_impl::submit_command_buffers) arrives here at
    // render_pass_end -- both are valid entry points.
    const VkResult result = present_image(m_acquired_image_index);

    if ((result == VK_SUBOPTIMAL_KHR) || (result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)) {
        init_swapchain();
        m_state = Swapchain_frame_state::image_present;
    } else if ((result == VK_ERROR_SURFACE_LOST_KHR) || (result == VK_ERROR_DEVICE_LOST) || (result == VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT)) {
        // Already logged by present_image, which also marked the
        // swapchain invalid. Do NOT route through device_message:
        // the editor's error callback turns these into ERHE_FATAL,
        // and lifecycle-driven surface loss is recoverable - as is
        // PRESENT_TIMING_QUEUE_FULL (recreation resets the timing
        // queue). Leave m_state at idle so wait_frame can pick the
        // rebuild path when the surface comes back.
        m_state = Swapchain_frame_state::idle;
    } else if (result != VK_SUCCESS) {
        m_device_impl.get_device().device_message(
            Message_severity::error,
            fmt::format("presenting frame failed with {} {}", static_cast<int32_t>(result), c_str(result))
        );
        m_state = Swapchain_frame_state::idle;
    } else {
        m_state = Swapchain_frame_state::image_present;
    }
    ERHE_VULKAN_SYNC_TRACE(
        "[SWAPCHAIN_END] frame_index={} acquired_image_index={} present_result={}",
        m_device_impl.get_frame_index(), m_acquired_image_index, static_cast<int32_t>(result)
    );
    return is_valid();
}

auto Swapchain_impl::has_maintenance1() const -> bool
{
    return m_device_impl.get_capabilities().m_swapchain_maintenance1;
}

auto Swapchain_impl::is_valid() const -> bool
{
    return m_surface_impl.is_valid() && !m_surface_impl.is_empty();
}

//////////////////////////////////////////////////////////////////////////////////////////////////

auto Swapchain_impl::get_semaphore() -> VkSemaphore
{
    return m_device_impl.get_sync_pool().get_semaphore();
}

auto Swapchain_impl::get_fence() -> VkFence
{
    return m_device_impl.get_sync_pool().get_fence();
}

void Swapchain_impl::recycle_semaphore(const VkSemaphore semaphore)
{
    m_device_impl.get_sync_pool().recycle_semaphore(semaphore);
}

void Swapchain_impl::recycle_fence(const VkFence fence)
{
    m_device_impl.get_sync_pool().recycle_fence(fence);
}

void Swapchain_impl::cleanup_swapchain_objects(Swapchain_objects& garbage)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    for (const VkImageView view : garbage.views) {
        vkDestroyImageView(vulkan_device, view, nullptr);
    }
    for (const VkFramebuffer framebuffer : garbage.framebuffers) {
        vkDestroyFramebuffer(vulkan_device, framebuffer, nullptr);
    }
    destroy_depth_image(garbage);

    garbage.images      .clear();
    garbage.views       .clear();
    garbage.framebuffers.clear();
}

void Swapchain_impl::add_present_to_history(const uint32_t index, const VkFence present_fence)
{
    Swapchain_frame_in_flight& frame = m_submit_history[current_slot()];

    m_present_history.emplace_back();
    m_present_history.back().present_semaphore = frame.present_semaphore;
    m_present_history.back().old_swapchains    = std::move(m_old_swapchains);

    frame.present_semaphore = VK_NULL_HANDLE;

    if (has_maintenance1()) {
        m_present_history.back().image_index   = INVALID_IMAGE_INDEX;
        m_present_history.back().cleanup_fence = present_fence;
    } else {
        // The fence needed to know when the semaphore can be recycled will be one that is
        // passed to vkAcquireNextImageKHR that returns the same image index. That is why
        // the image index needs to be tracked in this case.
        m_present_history.back().image_index = index;
    }
}

void Swapchain_impl::cleanup_present_history()
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    while (!m_present_history.empty()) {
        Present_history_entry& entry = m_present_history.front();

        // If there is no fence associated with the history, it can't be cleaned up yet.
        if (entry.cleanup_fence == VK_NULL_HANDLE) {
            // Can't have an old present operation without a fence that doesn't have an
            // image index used to later associate a fence with it.
            ERHE_VERIFY(entry.image_index != INVALID_IMAGE_INDEX);
            break;
        }

        // Otherwise check to see if the fence is signaled.
        const VkResult result = vkGetFenceStatus(vulkan_device, entry.cleanup_fence);
        if (result == VK_NOT_READY) {
            // Not yet
            break;
        } else if (result == VK_ERROR_DEVICE_LOST) {
            // Device-lost is expected on Android when the OS reclaims the
            // GPU during background, and on any platform if the driver
            // resets. The fence will never signal; drop the entry,
            // invalidate the swapchain, and let the lifecycle path
            // rebuild it on resume.
            log_swapchain->warn(
                "vkGetFenceStatus() returned {} {}; abandoning present-history entry",
                static_cast<int32_t>(result), c_str(result)
            );
            m_is_valid = false;
            cleanup_present_info(entry);
            m_present_history.pop_front();
            break;
        } else if (result != VK_SUCCESS) {
            log_swapchain->critical("vkGetFenceStatus() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        cleanup_present_info(entry);
        m_present_history.pop_front();
    }

    // The present history can grow indefinitely if a present operation is done on an index
    // that's never acquired in the future.  In that case, there's no fence associated with that
    // present operation.  Move the offending entry to last, so the resources associated with
    // the rest of the present operations can be duly freed.
    if (
        (m_present_history.size() > m_swapchain_objects.images.size() * 2) &&
        (m_present_history.front().cleanup_fence == VK_NULL_HANDLE)
    ) {
        Present_history_entry entry = std::move(m_present_history.front());
        m_present_history.pop_front();

        // We can't be stuck on a presentation to an old swapchain without a fence.
        assert(entry.image_index != INVALID_IMAGE_INDEX);

        // Move clean up data to the next (now first) present operation, if any.  Note that
        // there cannot be any clean up data on the rest of the present operations, because
        // the first present already gathers every old swapchain to clean up.
        assert(
            std::ranges::all_of(
                m_present_history, [](const Present_history_entry& entry) {
                    return entry.old_swapchains.empty();
                }
            )
        );
        m_present_history.front().old_swapchains = std::move(entry.old_swapchains);

        // Put the present operation at the end of the queue, so it's revisited after the
        // rest of the present operations are cleaned up.
        m_present_history.push_back(std::move(entry));
    }
}

void Swapchain_impl::cleanup_present_info(Present_history_entry& entry)
{
    // Called when it's safe to destroy resources associated with a present operation.
    if (entry.cleanup_fence != VK_NULL_HANDLE) {
        recycle_fence(entry.cleanup_fence);
    }

    // On the first acquire of the image, a fence is used but there is no present semaphore to
    // clean up.  That fence is placed in the present history just for clean up purposes.
    if (entry.present_semaphore != VK_NULL_HANDLE) {
        recycle_semaphore(entry.present_semaphore);
    }

    // Destroy old swapchains
    for (Swapchain_cleanup_data& old_swapchain : entry.old_swapchains) {
        cleanup_old_swapchain(old_swapchain);
    }

    entry.cleanup_fence     = VK_NULL_HANDLE;
    entry.present_semaphore = VK_NULL_HANDLE;
    entry.old_swapchains.clear();
    entry.image_index = std::numeric_limits<uint32_t>::max();
}

void Swapchain_impl::cleanup_old_swapchain(Swapchain_cleanup_data& old_swapchain)
{
    if (old_swapchain.swapchain != VK_NULL_HANDLE) {
        const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
        vkDestroySwapchainKHR(vulkan_device, old_swapchain.swapchain, nullptr);
    }

    for (VkSemaphore semaphore : old_swapchain.semaphores) {
        recycle_semaphore(semaphore);
    }

    old_swapchain.swapchain = VK_NULL_HANDLE;
    old_swapchain.semaphores.clear();
}

void Swapchain_impl::associate_fence_with_present_history(const uint32_t index, const VkFence acquire_fence)
{
    // The history looks like this:
    //
    // <entries for old swapchains, imageIndex == UINT32_MAX> <entries for this swapchain>
    //
    // Walk the list backwards and find the entry for the given image index. That's the last
    // present with that image. Associate the fence with that present operation.
    for (size_t entry_index = 0, end = m_present_history.size(); entry_index < end; ++entry_index) {
        Present_history_entry& entry = m_present_history[end - entry_index - 1];
        if (entry.image_index == INVALID_IMAGE_INDEX) {
            // No previous presentation with this index.
            break;
        }

        if (entry.image_index == index) {
            assert(entry.cleanup_fence == VK_NULL_HANDLE);
            entry.cleanup_fence = acquire_fence;
            return;
        }
    }

    // If no previous presentation with this index, add an empty entry just so the fence can be
    // cleaned up.
    m_present_history.emplace_back();
    m_present_history.back().cleanup_fence = acquire_fence;
    m_present_history.back().image_index   = index;
}

void Swapchain_impl::schedule_old_swapchain_for_destruction(const VkSwapchainKHR old_swapchain)
{
    // NOTE: do NOT destroy `old_swapchain` immediately even when m_present_history
    // looks "empty of non-sentinel entries". With VK_KHR_swapchain_maintenance1
    // every present inserts an entry with image_index == INVALID_IMAGE_INDEX
    // (the sentinel means "fence-tracked present", not "no present"), so an
    // early-destroy would tear down the VkSwapchainKHR while MoltenVK still has
    // an MTLCommandBuffer schedule-handler block referencing the underlying
    // MVKSwapchain -- we previously saw that surface as EXC_BAD_ACCESS in
    // MVKSwapchain::beginPresentation on com.Metal.CompletionQueueDispatch.
    //
    // Instead always defer to m_old_swapchains. The next add_present_to_history
    // move-grabs m_old_swapchains into the new present's old_swapchains list,
    // and cleanup_present_info destroys each only once the present fence has
    // signalled. If no further present happens, ~Swapchain_impl drains
    // m_old_swapchains at shutdown.

    Swapchain_cleanup_data cleanup_data{
        .swapchain = old_swapchain
    };

    // Place any present operation that's not associated with a fence into old_swapchains.  That
    // gets scheduled for destruction when the semaphore of the first image of the next
    // swapchain can be recycled.
    std::vector<Present_history_entry> history_to_keep;
    while (!m_present_history.empty()) {
        Present_history_entry& entry = m_present_history.back();

        // If this is about an older swapchain, let it be.
        if (entry.image_index == INVALID_IMAGE_INDEX) {
            ERHE_VERIFY(entry.cleanup_fence != VK_NULL_HANDLE);
            break;
        }

        // Reset the index, so it's not processed in the future.
        entry.image_index = INVALID_IMAGE_INDEX;

        if (entry.cleanup_fence != VK_NULL_HANDLE) {
            // If there is already a fence associated with it, let it be cleaned up once
            // the fence is signaled.
            history_to_keep.push_back(std::move(entry));
        } else {
            ERHE_VERIFY(entry.present_semaphore != VK_NULL_HANDLE);

            // Otherwise accumulate it in cleanup data.
            cleanup_data.semaphores.push_back(entry.present_semaphore);

            // Accumulate any previous swapchains that are pending destruction too.
            for (Swapchain_cleanup_data& swapchain : entry.old_swapchains) {
                m_old_swapchains.emplace_back(swapchain);
            }
            entry.old_swapchains.clear();
        }

        m_present_history.pop_back();
    }
    std::move(history_to_keep.begin(), history_to_keep.end(), std::back_inserter(m_present_history));

    if (cleanup_data.swapchain != VK_NULL_HANDLE || !cleanup_data.semaphores.empty()) {
        m_old_swapchains.emplace_back(std::move(cleanup_data));
    }

}

void Swapchain_impl::init_swapchain_image(const uint32_t index)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult       result        = VK_SUCCESS;

    ERHE_VERIFY(m_swapchain_objects.views[index] == VK_NULL_HANDLE);

    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(m_swapchain_objects.images[index]),
        fmt::format("Swapchain image {}", index).c_str()
    );

    const VkImageViewCreateInfo view_create_info{
        .sType              = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .image              = m_swapchain_objects.images[index],
        .viewType           = VK_IMAGE_VIEW_TYPE_2D,
        .format             = m_swapchain_format,
        .components = {
            .r              = VK_COMPONENT_SWIZZLE_R,
            .g              = VK_COMPONENT_SWIZZLE_G,
            .b              = VK_COMPONENT_SWIZZLE_B,
            .a              = VK_COMPONENT_SWIZZLE_A
        },
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    result = vkCreateImageView(vulkan_device, &view_create_info, nullptr, &m_swapchain_objects.views[index]);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateImageView() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_IMAGE_VIEW,
        reinterpret_cast<uint64_t>(m_swapchain_objects.views[index]),
        fmt::format("Swapchain image view {}", index).c_str()
    );
}

void Swapchain_impl::init_swapchain_framebuffer(const uint32_t index, VkRenderPass render_pass)
{
    ERHE_VERIFY(render_pass != VK_NULL_HANDLE);

    // Schedule old framebuffer for destruction if render pass has changed.
    if (
        (m_swapchain_objects.framebuffers[index] != VK_NULL_HANDLE) &&
        (m_swapchain_objects.render_pass[index] != render_pass)
    ) {
        Swapchain_objects need_cleanup{};
        need_cleanup.framebuffers.push_back(m_swapchain_objects.framebuffers[index]);
        m_submit_history[current_slot()].swapchain_garbage.push_back(std::move(need_cleanup));
        m_swapchain_objects.framebuffers[index] = VK_NULL_HANDLE;
    }

    std::array<VkImageView, 2> attachments = {
        m_swapchain_objects.views[index],
        m_swapchain_objects.depth_view
    };
    const uint32_t attachment_count = (m_swapchain_objects.depth_view != VK_NULL_HANDLE) ? 2u : 1u;

    const VkFramebufferCreateInfo framebuffer_create_info{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = 0,
        .flags           = 0,
        .renderPass      = render_pass,
        .attachmentCount = attachment_count,
        .pAttachments    = attachments.data(),
        .width           = m_swapchain_extent.width,
        .height          = m_swapchain_extent.height,
        .layers          = 1
    };

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult       result        = VK_SUCCESS;

    result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &m_swapchain_objects.framebuffers[index]);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateFramebuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    m_swapchain_objects.render_pass[index] = render_pass;
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_FRAMEBUFFER,
        reinterpret_cast<uint64_t>(m_swapchain_objects.framebuffers[index]),
        fmt::format("Swapchain framebuffer {}", index).c_str()
    );
}

void Swapchain_impl::init_swapchain()
{
    Vulkan_swapchain_create_info swapchain_create_info{};
    bool recreate_swapchain = m_surface_impl.update_swapchain(swapchain_create_info);
    bool surface_is_valid   = m_surface_impl.is_valid();
    bool surface_is_empty   = m_surface_impl.is_empty();
    if (!surface_is_valid) {
        log_context->debug("Surface is not ready to be used for rendering");
    }
    if (surface_is_empty) {
        log_context->debug("Surface is too small to have any visible pixels");
    }
    m_is_valid = surface_is_valid && !surface_is_empty;
    if (m_is_valid && recreate_swapchain) {
        init_swapchain(swapchain_create_info);
    }
}

void Swapchain_impl::init_swapchain(Vulkan_swapchain_create_info& swapchain_create_info)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult       result        = VK_SUCCESS;
    VkSwapchainKHR old_swapchain = m_vulkan_swapchain;

    log_context->debug(
        "Calling vkCreateSwapchainKHR(format = {}, colorSpace = {}, extent = {} x {}, presentMode {}, oldSwapchain = {})",
        c_str(swapchain_create_info.swapchain_create_info.imageFormat),
        c_str(swapchain_create_info.swapchain_create_info.imageColorSpace),
        swapchain_create_info.swapchain_create_info.imageExtent.width,
        swapchain_create_info.swapchain_create_info.imageExtent.height,
        c_str(swapchain_create_info.swapchain_create_info.presentMode),
        fmt::ptr(old_swapchain)
    );

    swapchain_create_info.swapchain_create_info.oldSwapchain = old_swapchain;
    result = vkCreateSwapchainKHR(vulkan_device, &swapchain_create_info.swapchain_create_info, nullptr, &m_vulkan_swapchain);
    ++m_swapchain_serial;
    m_max_present_id_submitted = 0; // present ids are per VkSwapchainKHR

    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateSwapchainKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_SWAPCHAIN_KHR,
        reinterpret_cast<uint64_t>(m_vulkan_swapchain),
        fmt::format("Swapchain {}", m_swapchain_serial)
    );

    init_present_timing(swapchain_create_info);

#if defined(VK_USE_PLATFORM_WIN32_KHR)
    // Exclusive fullscreen: the swapchain was created with
    // VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT chained
    // (Surface_impl::update_swapchain), so the mode must be acquired
    // explicitly. Failure is non-fatal: the swapchain then behaves like
    // normal fullscreen (e.g. INITIALIZATION_FAILED while another window
    // owns the display). Losing the mode later surfaces as
    // VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT from acquire/present,
    // handled like OUT_OF_DATE: recreate lands back here and re-acquires.
    // Release is implicit in swapchain destruction.
    if (swapchain_create_info.use_full_screen_exclusive && (vkAcquireFullScreenExclusiveModeEXT != nullptr)) {
        const VkResult fse_result = vkAcquireFullScreenExclusiveModeEXT(vulkan_device, m_vulkan_swapchain);
        if (fse_result == VK_SUCCESS) {
            log_swapchain->info("exclusive fullscreen mode acquired (VK_EXT_full_screen_exclusive)");
        } else {
            log_swapchain->warn(
                "vkAcquireFullScreenExclusiveModeEXT() returned {} {}; continuing without exclusive mode",
                static_cast<int32_t>(fse_result), c_str(fse_result)
            );
        }
    }
#endif

    // Schedule destruction of the old swapchain resources once this frame's submission is finished.
    m_submit_history[current_slot()].swapchain_garbage.push_back(std::move(m_swapchain_objects));

    // Schedule destruction of the old swapchain itself once its last presentation is finished.
    if (old_swapchain != VK_NULL_HANDLE) {
        schedule_old_swapchain_for_destruction(old_swapchain);
    }

    // Get the swapchain images.
    uint32_t image_count{0};
    result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }    

    m_swapchain_objects.images      .resize(image_count, VK_NULL_HANDLE);
    m_swapchain_objects.views       .resize(image_count, VK_NULL_HANDLE);
    m_swapchain_objects.render_pass .resize(image_count, VK_NULL_HANDLE);
    m_swapchain_objects.framebuffers.resize(image_count, VK_NULL_HANDLE);
    result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, m_swapchain_objects.images.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }    

    // init_swapchain_image() needs m_swapchain_format / m_swapchain_extent to create
    // the image views, so populate them before the (optional) eager-init loop and the
    // initial layout transition below.
    m_swapchain_extent = swapchain_create_info.swapchain_create_info.imageExtent;
    m_swapchain_format = swapchain_create_info.swapchain_create_info.imageFormat;

    if (!has_maintenance1()) {
        // When VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_EXT is used, image views
        // cannot be created until the first time the image is acquired.
        for (uint32_t index = 0; index < image_count; ++index) {
            init_swapchain_image(index);
        }
    }

    // The swapchain render pass declares initialLayout = UNDEFINED for the color
    // attachment (see vulkan_render_pass.cpp, swapchain branch) so no pre-transition
    // of the swapchain images from UNDEFINED to PRESENT_SRC_KHR is required here --
    // the render pass drives the discarding transition on its first use of each image.

    const Surface_create_info& surface_create_info = m_surface_impl.get_surface_create_info();
    if ((surface_create_info.context_window != nullptr) &&
        surface_create_info.context_window->get_window_configuration().use_depth) {
        m_depth_format = to_vulkan(m_device_impl.choose_depth_stencil_format(0, 1));
        create_depth_image(m_swapchain_extent.width, m_swapchain_extent.height);
    }
}

auto Swapchain_impl::get_surface_impl() -> Surface_impl&
{
    return m_surface_impl;
}

auto Swapchain_impl::get_command_buffer() -> VkCommandBuffer
{
    if (!is_valid()) {
        return VK_NULL_HANDLE;
    }

    const Device_frame_in_flight& device_frame = m_device_impl.get_device_frame_in_flight(current_slot());

    return device_frame.command_buffer;
}

auto Swapchain_impl::get_active_acquire_semaphore() const -> VkSemaphore
{
    return m_submit_history[current_slot()].acquire_semaphore;
}

auto Swapchain_impl::acquire_swapchain_framebuffer(VkRenderPass render_pass) -> VkFramebuffer
{
    if (!is_valid()) {
        return VK_NULL_HANDLE;
    }
    if (
        (m_swapchain_objects.framebuffers[m_acquired_image_index] == VK_NULL_HANDLE) ||
        (m_swapchain_objects.render_pass [m_acquired_image_index] != render_pass)
    ) {
        init_swapchain_framebuffer(m_acquired_image_index, render_pass);
    }
    return m_swapchain_objects.framebuffers[m_acquired_image_index];
}

auto Swapchain_impl::get_swapchain_extent() const -> VkExtent2D
{
    return m_swapchain_extent;
}

void Swapchain_impl::mark_render_pass_recorded()
{
    // The user-cb model records vkCmdBeginRenderPass2 / vkCmdEndRenderPass2
    // directly into the user's Command_buffer instead of going through
    // Swapchain_impl::begin_render_pass / end_render_pass. The state
    // machine still expects the image_acquired -> render_pass_begin ->
    // render_pass_end -> command_buffer_submit chain, so flip it forward
    // explicitly to satisfy the asserts on present.
    if (m_state == Swapchain_frame_state::image_ackquired) {
        m_state = Swapchain_frame_state::render_pass_end;
    }
}

auto Swapchain_impl::get_active_present_semaphore() const -> VkSemaphore
{
    return m_submit_history[current_slot()].present_semaphore;
}

void Swapchain_impl::setup_frame()
{
    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

    // For the frame we need:
    // - A fence for the submission
    // - A semaphore for image acquire
    // - A semaphore for image present

    // The slot is owned by Device_impl::m_frame_index (advanced exactly
    // once per Device_impl::end_frame in update_frame_completion). Snapshot
    // it here -- do not advance an independent counter, or the swapchain
    // and device sides will desynchronize whenever an end_frame happens
    // without a matching setup_frame (init-time prime, OpenXR-only ticks,
    // etc.).
    Swapchain_frame_in_flight& frame        = m_submit_history[current_slot()];
    Device_frame_in_flight&    device_frame = m_device_impl.get_device_frame_in_flight(current_slot());
    if (device_frame.submit_fence != VK_NULL_HANDLE) {
        result = vkWaitForFences(vulkan_device, 1, &device_frame.submit_fence, true, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkWaitForFences() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Reset/recycle resources, they are no longer in use.
        recycle_fence(device_frame.submit_fence);
        if (frame.acquire_semaphore_pending_recycle) {
            // A prior visit to this slot allocated an acquire_semaphore and
            // its swapchain submit consumed it. The submission is done --
            // either the fence wait above just confirmed it, or an
            // intervening device-only submit already drained the earlier
            // fence in ensure_device_frame_slot -- so the handle is idle
            // and safe to return to the pool.
            recycle_semaphore(frame.acquire_semaphore);
            frame.acquire_semaphore                = VK_NULL_HANDLE;
            frame.acquire_semaphore_pending_recycle = false;
        }
        m_device_impl.reset_device_frame_command_pool(current_slot());

        // Destroy any garbage that's associated with this submission.
        for (Swapchain_objects& garbage : frame.swapchain_garbage) {
            cleanup_swapchain_objects(garbage);
        }
        frame.swapchain_garbage.clear();

        // Note that while the submission fence, the semaphore it waited on and the command
        // pool its command was allocated from are guaranteed to have finished execution,
        // there is no guarantee that the present semaphore is not in use.
        //
        // This is because the fence wait above ensures that the submission _before_ present
        // is finished, but makes no guarantees as to the state of the present operation
        // that follows.  The present semaphore is queued for garbage collection when
        // possible after present, and is not kept as part of the submit history.
        ERHE_VERIFY(frame.present_semaphore == VK_NULL_HANDLE);
    }

    device_frame.submit_fence                = get_fence();
    ERHE_VERIFY(frame.acquire_semaphore == VK_NULL_HANDLE);
    frame.acquire_semaphore                  = get_semaphore();
    frame.acquire_semaphore_pending_recycle  = true;
    // present_semaphore is allocated in begin_frame() after a successful
    // image acquire, not here. This keeps init-time device frames that
    // never nest a swapchain frame from leaking a present_semaphore into
    // the slot (which would later trip the recycle-time VERIFY).

    m_device_impl.ensure_device_frame_command_buffer(current_slot());

    m_state = Swapchain_frame_state::command_buffer_ready;
}

void Swapchain_impl::init_present_timing(const Vulkan_swapchain_create_info& swapchain_create_info)
{
    m_present_timing_active              = false;
    m_present_at_absolute_time           = false;
    m_present_at_relative_time           = false;
    m_present_stage_queries_mask         = 0;
    m_timing_calibration_valid           = false;
    m_timing_calibration_queue_ops_valid = false;
    if (!m_device_impl.get_capabilities().m_present_timing) {
        return;
    }
    if (m_device_impl.get_frame_pacing_tier() == Frame_pacing_tier::slop_servo) {
        // Tier S (P4.2) runs WITHOUT present timing: the slop servo senses
        // backpressure only, and chaining timing requests on this tier's
        // plain-FIFO swapchain fills the per-swapchain timing queue on this
        // driver (measured: VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT within
        // ~32 presents at startup) - the entries do not complete the way
        // they do on fifo_latest_ready. Not needing the extension is the
        // method's contract; skipping it also keeps the tier honest for
        // A/B measurements. Without the refreshDuration timing query, the
        // display refresh period (the servo's overshoot reference and the
        // observer's grid seed) comes from the window's display mode.
        log_swapchain->info("present timing not enabled: frame pacing tier S does not use it");
        erhe::window::Context_window* const context_window = m_device_impl.get_context_window();
        if (context_window != nullptr) {
            const float refresh_rate = context_window->get_display_refresh_rate();
            if (refresh_rate > 0.0f) {
                m_device_impl.get_device().set_display_refresh_duration_seconds(1.0 / static_cast<double>(refresh_rate));
            }
        }
        return;
    }
    if ((vkSetSwapchainPresentTimingQueueSizeEXT == nullptr) ||
        (vkGetSwapchainTimingPropertiesEXT       == nullptr) ||
        (vkGetSwapchainTimeDomainPropertiesEXT   == nullptr)) {
        return;
    }
    VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    VkResult result = vkSetSwapchainPresentTimingQueueSizeEXT(vulkan_device, m_vulkan_swapchain, 32);
    if (result != VK_SUCCESS) {
        log_swapchain->warn(
            "vkSetSwapchainPresentTimingQueueSizeEXT() failed with {} {}; present timing disabled",
            static_cast<int32_t>(result), c_str(result)
        );
        return;
    }

    VkSwapchainTimingPropertiesEXT timing_properties{
        .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT,
        .pNext           = nullptr,
        .refreshDuration = 0,
        .refreshInterval = 0
    };
    uint64_t timing_properties_counter = 0;
    result = vkGetSwapchainTimingPropertiesEXT(vulkan_device, m_vulkan_swapchain, &timing_properties, &timing_properties_counter);
    if (result == VK_SUCCESS) {
        m_refresh_duration_ns = timing_properties.refreshDuration;
        m_device_impl.get_device().set_display_refresh_duration_seconds(
            static_cast<double>(m_refresh_duration_ns) * 1e-9
        );
    }

    // Time domains: two-call pattern; pick the calibrated host domain when
    // offered so feedback lands directly on the reference clock.
    VkSwapchainTimeDomainPropertiesEXT domain_properties{
        .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT,
        .pNext           = nullptr,
        .timeDomainCount = 0,
        .pTimeDomains    = nullptr,
        .pTimeDomainIds  = nullptr
    };
    uint64_t time_domains_counter = 0;
    result = vkGetSwapchainTimeDomainPropertiesEXT(vulkan_device, m_vulkan_swapchain, &domain_properties, &time_domains_counter);
    if ((result != VK_SUCCESS) || (domain_properties.timeDomainCount == 0)) {
        log_swapchain->warn("vkGetSwapchainTimeDomainPropertiesEXT() gave no time domains; present timing disabled");
        return;
    }
    std::vector<VkTimeDomainKHR> domains   (domain_properties.timeDomainCount);
    std::vector<uint64_t>        domain_ids(domain_properties.timeDomainCount);
    domain_properties.pTimeDomains   = domains.data();
    domain_properties.pTimeDomainIds = domain_ids.data();
    result = vkGetSwapchainTimeDomainPropertiesEXT(vulkan_device, m_vulkan_swapchain, &domain_properties, &time_domains_counter);
    if (result != VK_SUCCESS) {
        log_swapchain->warn(
            "vkGetSwapchainTimeDomainPropertiesEXT() failed with {} {}; present timing disabled",
            static_cast<int32_t>(result), c_str(result)
        );
        return;
    }
#if defined(_WIN32)
    constexpr VkTimeDomainKHR preferred_domain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
#else
    constexpr VkTimeDomainKHR preferred_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
#endif
    std::size_t chosen = 0;
    for (std::size_t i = 0; i < domains.size(); ++i) {
        log_swapchain->info(
            "Present timing time domain [{}]: domain={} id={}",
            i, static_cast<int32_t>(domains[i]), domain_ids[i]
        );
        if (domains[i] == preferred_domain) {
            chosen = i;
        }
    }
    m_timing_time_domain    = domains[chosen];
    m_timing_time_domain_id = domain_ids[chosen];

    // Present stages supported by this surface; prefer the stage closest to
    // light hitting the eye.
    VkPresentStageFlagsEXT supported_stages = 0;
    if (m_device_impl.get_capabilities().m_surface_capabilities2 && (vkGetPhysicalDeviceSurfaceCapabilities2KHR != nullptr)) {
        VkPresentTimingSurfaceCapabilitiesEXT timing_surface_capabilities{
            .sType                          = VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT,
            .pNext                          = nullptr,
            .presentTimingSupported         = VK_FALSE,
            .presentAtAbsoluteTimeSupported = VK_FALSE,
            .presentAtRelativeTimeSupported = VK_FALSE,
            .presentStageQueries            = 0
        };
        VkSurfaceCapabilities2KHR surface_capabilities2{
            .sType               = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
            .pNext               = &timing_surface_capabilities,
            .surfaceCapabilities = {}
        };
        VkPhysicalDeviceSurfaceInfo2KHR surface_info{
            .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
            .pNext   = nullptr,
            .surface = m_surface_impl.get_vulkan_surface()
        };
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        // Surface capabilities are per fullscreen-exclusive mode: without
        // the chained VkSurfaceFullScreenExclusiveInfoEXT the driver
        // reports DEFAULT-mode caps, which can under-report what
        // application-controlled exclusive supports (found while
        // verifying the P2.3 target-time modes). Query the mode this
        // swapchain was actually created for.
        VkSurfaceFullScreenExclusiveWin32InfoEXT query_fse_win32_info{};
        VkSurfaceFullScreenExclusiveInfoEXT      query_fse_info{};
        if (swapchain_create_info.use_full_screen_exclusive) {
            query_fse_win32_info       = swapchain_create_info.full_screen_exclusive_win32_info;
            query_fse_win32_info.pNext = nullptr;
            query_fse_info             = swapchain_create_info.full_screen_exclusive_info;
            query_fse_info.pNext       = &query_fse_win32_info;
            surface_info.pNext         = &query_fse_info;
        }
#endif
        const VkResult caps_result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(
            m_device_impl.get_vulkan_physical_device(), &surface_info, &surface_capabilities2
        );
        if (caps_result == VK_SUCCESS) {
            supported_stages = timing_surface_capabilities.presentStageQueries;
            // FR3 target-time modes (step P2.3): device feature AND surface
            // capability, both required by the valid-usage rules.
            m_present_at_absolute_time =
                m_device_impl.get_capabilities().m_present_at_absolute_time &&
                (timing_surface_capabilities.presentAtAbsoluteTimeSupported == VK_TRUE);
            m_present_at_relative_time =
                m_device_impl.get_capabilities().m_present_at_relative_time &&
                (timing_surface_capabilities.presentAtRelativeTimeSupported == VK_TRUE);
        }
    }
    log_swapchain->info(
        "Present timing surface stage-query mask = 0x{:x} (0x1 queue_ops_end, 0x2 request_dequeued, 0x4 first_pixel_out, 0x8 first_pixel_visible)",
        static_cast<uint32_t>(supported_stages)
    );
    if ((supported_stages & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT) != 0) {
        m_present_stage_queries = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT;
    } else if ((supported_stages & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT) != 0) {
        m_present_stage_queries = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
    } else if ((supported_stages & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) != 0) {
        m_present_stage_queries = VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT;
    } else {
        m_present_stage_queries = 0;
    }
    if (m_present_stage_queries == 0) {
        log_swapchain->warn("Present timing: no usable present stage queries; present timing disabled");
        return;
    }
    // Query every supported stage per present; the primary stage above
    // stays authoritative for achieved_present_time and the pacer grid.
    m_present_stage_queries_mask = supported_stages;

    m_present_timing_active = true;
    log_swapchain->info(
        "Present timing enabled: refresh_duration={} ns time_domain={} time_domain_id={} stage=0x{:x} target_absolute={} target_relative={}",
        m_refresh_duration_ns,
        static_cast<int32_t>(m_timing_time_domain),
        m_timing_time_domain_id,
        static_cast<uint32_t>(m_present_stage_queries),
        m_present_at_absolute_time,
        m_present_at_relative_time
    );
}

void Swapchain_impl::update_present_timing_calibration()
{
    if (!m_present_timing_active) {
        return;
    }
    if (
        (m_timing_time_domain != VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT) &&
        (m_timing_time_domain != VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT)
    ) {
        return; // host time domain - no calibration needed
    }
    const uint64_t frame_index = m_device_impl.get_frame_index();
    if (m_timing_calibration_valid && ((frame_index - m_timing_calibration_frame) < 120)) {
        return;
    }
    const PFN_vkGetCalibratedTimestampsKHR get_calibrated_timestamps =
        (vkGetCalibratedTimestampsKHR != nullptr) ? vkGetCalibratedTimestampsKHR : vkGetCalibratedTimestampsEXT;
    if (get_calibrated_timestamps == nullptr) {
        return;
    }
#if defined(_WIN32)
    constexpr VkTimeDomainKHR host_domain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
#else
    constexpr VkTimeDomainKHR host_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
#endif
    const bool with_queue_ops =
        ((m_present_stage_queries_mask & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) != 0) &&
        (m_present_stage_queries != VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT);
    const VkSwapchainCalibratedTimestampInfoEXT swapchain_info{
        .sType        = VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT,
        .pNext        = nullptr,
        .swapchain    = m_vulkan_swapchain,
        .presentStage = m_present_stage_queries,
        .timeDomainId = m_timing_time_domain_id
    };
    // Stage-local clocks have per-stage epochs: calibrate the diagnostic
    // QUEUE_OPERATIONS_END stage with its own pair in the same call.
    const VkSwapchainCalibratedTimestampInfoEXT swapchain_queue_ops_info{
        .sType        = VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT,
        .pNext        = nullptr,
        .swapchain    = m_vulkan_swapchain,
        .presentStage = VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT,
        .timeDomainId = m_timing_time_domain_id
    };
    const VkCalibratedTimestampInfoKHR infos[2] = {
        {
            .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
            .pNext      = &swapchain_info,
            .timeDomain = m_timing_time_domain
        },
        {
            .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
            .pNext      = nullptr,
            .timeDomain = host_domain
        }
    };
    uint64_t timestamps[2] = {0, 0};
    uint64_t max_deviation = 0;
    const VkResult result = get_calibrated_timestamps(m_device_impl.get_vulkan_device(), 2, infos, timestamps, &max_deviation);
    if (result != VK_SUCCESS) {
        if (!m_timing_calibration_valid) {
            log_swapchain->warn(
                "vkGetCalibratedTimestamps() for the present-stage time domain failed with {} {}",
                static_cast<int32_t>(result), c_str(result)
            );
        }
        return;
    }
    m_timing_calibration_domain_value = timestamps[0];
    m_timing_calibration_host_seconds = m_device_impl.host_calibrated_value_to_seconds(timestamps[1]);
    m_timing_calibration_frame        = frame_index;
    m_timing_calibration_valid        = true;
    if (with_queue_ops) {
        // Time domains must be unique within one vkGetCalibratedTimestamps
        // call (VUID 09246), so the QUEUE_OPERATIONS_END stage clock gets
        // its own call: same stage-local domain id, different stage epoch.
        const VkCalibratedTimestampInfoKHR queue_ops_infos[2] = {
            {
                .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                .pNext      = &swapchain_queue_ops_info,
                .timeDomain = m_timing_time_domain
            },
            {
                .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                .pNext      = nullptr,
                .timeDomain = host_domain
            }
        };
        uint64_t queue_ops_timestamps[2] = {0, 0};
        uint64_t queue_ops_deviation    = 0;
        const VkResult queue_ops_result = get_calibrated_timestamps(
            m_device_impl.get_vulkan_device(), 2, queue_ops_infos, queue_ops_timestamps, &queue_ops_deviation
        );
        if (queue_ops_result == VK_SUCCESS) {
            // Re-base onto the primary pair's host epoch: convert via this
            // call's own host timestamp to keep the pairs consistent.
            const double host_delta_seconds =
                m_device_impl.host_calibrated_value_to_seconds(queue_ops_timestamps[1]) - m_timing_calibration_host_seconds;
            m_timing_calibration_queue_ops_value =
                queue_ops_timestamps[0] - static_cast<uint64_t>(static_cast<int64_t>(host_delta_seconds * 1e9));
            m_timing_calibration_queue_ops_valid = true;
        }
    }
}

void Swapchain_impl::poll_present_timing()
{
    if (!m_present_timing_active || (vkGetPastPresentationTimingEXT == nullptr)) {
        return;
    }
    update_present_timing_calibration();
    VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    for (std::size_t i = 0; i < m_past_timings.size(); ++i) {
        m_past_timings[i] = VkPastPresentationTimingEXT{
            .sType             = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT,
            .pNext             = nullptr,
            .presentId         = 0,
            .targetTime        = 0,
            .presentStageCount = static_cast<uint32_t>(m_past_timing_stages[i].size()),
            .pPresentStages    = m_past_timing_stages[i].data(),
            .timeDomain        = VK_TIME_DOMAIN_DEVICE_KHR,
            .timeDomainId      = 0,
            .reportComplete    = VK_FALSE
        };
    }
    const VkPastPresentationTimingInfoEXT info{
        .sType     = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT,
        .pNext     = nullptr,
        .flags     = VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT,
        .swapchain = m_vulkan_swapchain
    };
    VkPastPresentationTimingPropertiesEXT properties{
        .sType                   = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT,
        .pNext                   = nullptr,
        .timingPropertiesCounter = 0,
        .timeDomainsCounter      = 0,
        .presentationTimingCount = static_cast<uint32_t>(m_past_timings.size()),
        .pPresentationTimings    = m_past_timings.data()
    };
    const VkResult result = vkGetPastPresentationTimingEXT(vulkan_device, &info, &properties);
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
        return;
    }
    for (uint32_t i = 0; i < properties.presentationTimingCount; ++i) {
        const VkPastPresentationTimingEXT& timing = m_past_timings[i];
        if (timing.presentId == 0) {
            continue;
        }
        uint64_t primary_time   = 0;
        uint64_t queue_ops_time = 0;
        for (uint32_t s = 0; s < timing.presentStageCount; ++s) {
            const VkPresentStageTimeEXT& stage = timing.pPresentStages[s];
            if ((stage.stage & m_present_stage_queries) != 0) {
                primary_time = stage.time;
            } else if (stage.stage == VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) {
                queue_ops_time = stage.time;
            }
        }
        if (primary_time == 0) {
            continue;
        }
        // Converts a stage time in the feedback's time domain to reference-
        // clock seconds; stage-local domains have per-stage epochs, so each
        // stage brings its own calibration pair. Returns 0.0 when the
        // domain cannot be converted (yet).
        const auto stage_time_to_seconds = [&](
            const uint64_t stage_time,
            const bool     calibration_valid,
            const uint64_t calibration_domain_value
        ) -> double {
            if (timing.timeDomain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR) {
                return static_cast<double>(stage_time) * 1e-9;
            }
#if defined(_WIN32)
            if (timing.timeDomain == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR) {
                return m_device_impl.host_calibrated_value_to_seconds(stage_time);
            }
#endif
            if (
                (timing.timeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT) ||
                (timing.timeDomain == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT)
            ) {
                if (!calibration_valid) {
                    return 0.0;
                }
                // Domain units are nanoseconds; offset against the calibrated pair.
                const double delta_ns = static_cast<double>(
                    static_cast<int64_t>(stage_time - calibration_domain_value)
                );
                return m_timing_calibration_host_seconds + (delta_ns * 1e-9);
            }
            return 0.0; // unhandled domain; refinement later
        };
        const double achieved_seconds = stage_time_to_seconds(primary_time, m_timing_calibration_valid, m_timing_calibration_domain_value);
        if (achieved_seconds <= 0.0) {
            continue;
        }
        // presentId is device frame index + 1 (see present_image).
        const std::int64_t frame_id = static_cast<std::int64_t>(timing.presentId) - 1;
        if (erhe::frame_pacing::Frame_time_record* record = m_device_impl.get_frame_time_recorder().find(frame_id)) {
            record->achieved_present_time = achieved_seconds;
            if (queue_ops_time != 0) {
                const double queue_ops_seconds = stage_time_to_seconds(
                    queue_ops_time, m_timing_calibration_queue_ops_valid, m_timing_calibration_queue_ops_value
                );
                if (queue_ops_seconds > 0.0) {
                    record->achieved_queue_ops_time = queue_ops_seconds;
                }
            }
        }
        if (!m_present_feedback_logged) {
            m_present_feedback_logged = true;
            log_swapchain->info(
                "First present timing feedback: present_id={} stage=0x{:x} time_domain={} complete={}",
                timing.presentId,
                static_cast<uint32_t>(m_present_stage_queries),
                static_cast<int32_t>(timing.timeDomain),
                timing.reportComplete == VK_TRUE
            );
        }
    }
}

auto Swapchain_impl::acquire_next_image(uint32_t* index) -> VkResult
{
    VkDevice                   vulkan_device = m_device_impl.get_vulkan_device();
    Swapchain_frame_in_flight& frame         = m_submit_history[current_slot()];

    // Use a fence to know when acquire is done.  Without VK_EXT_swapchain_maintenance1, this
    // fence is used to infer when the _previous_ present to this image index has finished.
    // There is no need for this with VK_EXT_swapchain_maintenance1.
    VkFence acquire_fence = has_maintenance1() ? VK_NULL_HANDLE : get_fence();

    erhe::frame_pacing::Frame_time_record* const time_record =
        m_device_impl.get_frame_time_recorder().find(static_cast<std::int64_t>(m_device_impl.get_frame_index()));
    if ((time_record != nullptr) && (time_record->acquire_begin == 0.0)) {
        time_record->acquire_begin = erhe::frame_pacing::Frame_time_recorder::now();
    }
    VkResult result = vkAcquireNextImageKHR(vulkan_device, m_vulkan_swapchain, UINT64_MAX, frame.acquire_semaphore, acquire_fence, index);
    if ((time_record != nullptr) && (time_record->acquire_end == 0.0)) {
        time_record->acquire_end = erhe::frame_pacing::Frame_time_recorder::now();
    }

    if (has_maintenance1() && ((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR))) {
        // When VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_EXT is specified, image
        // views must be created after the first time the image is acquired.
        assert(*index < m_swapchain_objects.views.size());
        if (m_swapchain_objects.views[*index] == VK_NULL_HANDLE) {
            init_swapchain_image(*index);
        }
    }

    if (!has_maintenance1()) {
        if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
            // If failed, fence is untouched, recycle it.
            //
            // The semaphore is also untouched, but it may be used in the retry of
            // vkAcquireNextImageKHR. It is nevertheless cleaned up after cpu
            // throttling automatically.
            recycle_fence(acquire_fence);
            return result;
        }

        associate_fence_with_present_history(*index, acquire_fence);
    }

    // TODO Add handling for pretransform suboptimal
    return result;
}

auto Swapchain_impl::present_image(uint32_t index) -> VkResult
{
    VkQueue                    vulkan_present_queue = m_device_impl.get_present_queue();
    Swapchain_frame_in_flight& frame                = m_submit_history[current_slot()];

    VkPresentInfoKHR present_info{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &frame.present_semaphore,
        .swapchainCount     = 1,
        .pSwapchains        = &m_vulkan_swapchain,
        .pImageIndices      = &index,
        .pResults           = nullptr
    };

    // When VK_EXT_swapchain_maintenance1 is enabled, add a fence to the present operation,
    // which is signaled when the resources associated with present operation can be freed.
    VkFence                        present_fence = VK_NULL_HANDLE;
    VkSwapchainPresentFenceInfoEXT fence_info{
        .sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .pNext          = nullptr,
        .swapchainCount = 0,
        .pFences        = nullptr
    };
    VkSwapchainPresentModeInfoEXT  present_mode{
        .sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT,
        .pNext          = nullptr,
        .swapchainCount = 0,
        .pPresentModes  = nullptr
    };
    if (has_maintenance1()){
        present_fence = get_fence();

        fence_info.swapchainCount = 1;
        fence_info.pFences        = &present_fence;

        present_info.pNext = &fence_info;

        // If present mode has changed but the two modes are compatible, change the present
        // mode at present time.
        //if (current_present_mode != desired_present_mode) {
        //    // Can't reach here if the modes are not compatible.
        //    assert(are_present_modes_compatible());
        //
        //    current_present_mode = desired_present_mode;
        //
        //    present_mode.swapchainCount = 1;
        //    present_mode.pPresentModes  = &current_present_mode;
        //
        //    fence_info.pNext = &present_mode;
        //}
    }

    // Present id chaining (implementation plan step P0.4): id is the device
    // frame index + 1 (ids must be non-zero and increasing), so feedback
    // correlates to a frame record without a lookup table. Also the id
    // vkWaitForPresentKHR will use for the FR5 clamp later.
    uint64_t       present_id_value = 0;
    VkPresentIdKHR present_id_info{
        .sType          = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .pNext          = nullptr,
        .swapchainCount = 1,
        .pPresentIds    = &present_id_value
    };
    VkPresentId2KHR present_id2_info{
        .sType          = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR,
        .pNext          = nullptr,
        .swapchainCount = 1,
        .pPresentIds    = &present_id_value
    };
    if (m_device_impl.get_capabilities().m_present_id) {
        present_id_value = m_device_impl.get_frame_index() + 1;
        // The legacy chain is the verified-working path on the current
        // NVIDIA driver; use id2 only when the legacy extension is absent.
        // (Both were exercised while bisecting the present-timing crash
        // below; the id chains themselves are fine.)
        if (m_device_impl.get_device_extensions().m_VK_KHR_present_id) {
            present_id_info.pNext = present_info.pNext;
            present_info.pNext    = &present_id_info;
        } else if (m_device_impl.get_device_extensions().m_VK_KHR_present_id2) {
            present_id2_info.pNext = present_info.pNext;
            present_info.pNext     = &present_id2_info;
        }
    }
    // Present timing: ask the presentation engine to record when the image
    // reached the chosen present stage (feedback into the records, P0.4),
    // and chain the pacer's target present time when one was requested for
    // this frame (FR3, step P2.3). The pacer targets a vsync slot time, so
    // NEAREST_REFRESH_CYCLE lets the engine absorb small precision errors
    // instead of pushing the present a full cycle late.
    VkPresentTimingInfoEXT timing_info{
        .sType                        = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT,
        .pNext                        = nullptr,
        .flags                        = 0,
        .targetTime                   = 0,
        .timeDomainId                 = m_timing_time_domain_id,
        .presentStageQueries          = (m_present_stage_queries_mask != 0) ? m_present_stage_queries_mask : m_present_stage_queries,
        .targetTimeDomainPresentStage = 0
    };
    const double target_seconds = m_device_impl.get_present_target_time(static_cast<std::int64_t>(m_device_impl.get_frame_index()));
    if (m_present_timing_active && (target_seconds > 0.0)) {
        const bool domain_is_calibrated =
            (m_timing_time_domain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT) ||
            (m_timing_time_domain == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT);
        if (m_present_at_absolute_time && (!domain_is_calibrated || m_timing_calibration_valid)) {
            // Absolute target in the swapchain timing domain: invert the
            // feedback calibration (poll_present_timing) for the
            // present-stage-local domains, or convert directly for host
            // domains.
            update_present_timing_calibration();
            if (domain_is_calibrated) {
                const double delta_ns = (target_seconds - m_timing_calibration_host_seconds) * 1e9;
                timing_info.targetTime = m_timing_calibration_domain_value + static_cast<uint64_t>(static_cast<int64_t>(delta_ns));
            } else if (m_timing_time_domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR) {
                timing_info.targetTime = static_cast<uint64_t>(target_seconds * 1e9);
            } else {
                timing_info.targetTime = m_device_impl.reference_seconds_to_host_calibrated_value(target_seconds);
            }
            timing_info.flags = VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;
        } else if (m_present_at_relative_time) {
            // Relative target: duration from the present call itself, which
            // sidesteps the domain calibration entirely. The pacer's target
            // and the reference clock share the timeline, so the remaining
            // lead is the duration. A target already in the past degrades
            // to no request (present as soon as possible) - correct, since
            // the engine could not honor it anyway.
            const double lead_seconds = target_seconds - erhe::frame_pacing::Frame_time_recorder::now();
            if (lead_seconds > 0.0) {
                timing_info.targetTime = static_cast<uint64_t>(lead_seconds * 1e9);
                // Strict not-before semantics (no NEAREST_REFRESH_CYCLE).
                // Measured on the development machine (tier doc section 5):
                // this driver ignores the target entirely in every flag /
                // stage / domain combination, including a mid-cycle-biased
                // NEAREST probe, so the flag choice is inert there. Strict
                // is kept as the production formulation because it encodes
                // what the pacer wants from a conforming engine: never
                // display before the targeted cycle. On a driver that does
                // honor targets, revisit with a small early bias
                // (target - T/4) to absorb clock skew across the boundary
                // (see tier doc section 5).
                timing_info.flags = VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT;
            }
        }
        if ((timing_info.targetTime != 0) && domain_is_calibrated) {
            // VUID: with a present-stage-local target time domain the target
            // stage must be a single stage.
            timing_info.targetTimeDomainPresentStage = m_present_stage_queries;
        }
    }
    VkPresentTimingsInfoEXT timings_info{
        .sType          = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT,
        .pNext          = nullptr,
        .swapchainCount = 1,
        .pTimingInfos   = &timing_info
    };
    if (m_present_timing_active) {
        timings_info.pNext = present_info.pNext;
        present_info.pNext = &timings_info;
    }

    ERHE_VULKAN_TRACE(
        "vkQueuePresentKHR image_index={} present_sem=0x{:x} swapchain=0x{:x}",
        index,
        reinterpret_cast<std::uintptr_t>(frame.present_semaphore),
        reinterpret_cast<std::uintptr_t>(m_vulkan_swapchain)
    );
    VkResult result = vkQueuePresentKHR(vulkan_present_queue, &present_info);
    ERHE_VULKAN_TRACE("vkQueuePresentKHR returned {}", static_cast<int32_t>(result));
    if ((result == VK_ERROR_SURFACE_LOST_KHR) || (result == VK_ERROR_DEVICE_LOST) || (result == VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT)) {
        // SURFACE_LOST: the underlying ANativeWindow / NSWindow / HWND
        // backing the VkSurfaceKHR is gone. On Android this is the
        // background-event path (the OS detached our window) when the
        // SDL_AddEventWatch handler did not stop submission in time.
        // DEVICE_LOST: GPU reset / TDR. In both cases we mark the
        // swapchain invalid; the editor's lifecycle / resume logic is
        // responsible for rebuilding surface + swapchain when the
        // window is interactive again.
        // PRESENT_TIMING_QUEUE_FULL: presents stopped reaching the display
        // (observed live: alt-tab away from exclusive fullscreen), so the
        // per-swapchain present-timing queue never drained and this request
        // was rejected. The stuck entries can never complete; recreation
        // resets the timing queue.
        log_swapchain->warn(
            "vkQueuePresentKHR() returned {} {}; marking swapchain invalid and skipping present",
            static_cast<int32_t>(result), c_str(result)
        );
        m_is_valid = false;
        return result;
    }
    if ((result != VK_SUCCESS) && (result != VK_ERROR_OUT_OF_DATE_KHR) && (result != VK_SUBOPTIMAL_KHR) && (result != VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)) {
        log_swapchain->critical("vkQueuePresentKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    // Track the highest id actually presented on this swapchain so the
    // present-wait clamp only waits on ids that can unblock. OUT_OF_DATE is
    // excluded: the present may not have been executed, and recreation
    // resets the counter anyway.
    if ((present_id_value != 0) && ((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR))) {
        m_max_present_id_submitted = std::max(m_max_present_id_submitted, present_id_value);
    }

    add_present_to_history(index, present_fence);
    cleanup_present_history();
    poll_present_timing();

    return result;
}

auto Swapchain_impl::wait_for_displayed_frame(const std::int64_t frame_id, const uint64_t timeout_ns) -> Present_wait_result
{
    if ((frame_id < 0) || !is_valid() || (m_vulkan_swapchain == VK_NULL_HANDLE)) {
        return Present_wait_result::unsupported;
    }
    // Legacy VK_KHR_present_wait only: the wait must pair with the id chain
    // present_image() uses (legacy VkPresentIdKHR preferred there).
    // present_wait2 would additionally require the swapchain to be created
    // with VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR plus a surface
    // capability check; add that path if a legacy-less driver ever matters.
    if (!m_device_impl.get_capabilities().m_present_wait          ||
        !m_device_impl.get_device_extensions().m_VK_KHR_present_wait ||
        !m_device_impl.get_device_extensions().m_VK_KHR_present_id   ||
        (vkWaitForPresentKHR == nullptr))
    {
        return Present_wait_result::unsupported;
    }
    const uint64_t present_id = static_cast<uint64_t>(frame_id) + 1; // present_image(): presentId = device frame index + 1
    if (present_id > m_max_present_id_submitted) {
        // Not presented through this VkSwapchainKHR (startup, or right
        // after recreation); waiting could only time out.
        return Present_wait_result::unsupported;
    }
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    const VkResult result = vkWaitForPresentKHR(vulkan_device, m_vulkan_swapchain, present_id, timeout_ns);
    switch (result) {
        case VK_SUCCESS: {
            return Present_wait_result::displayed;
        }
        case VK_TIMEOUT: {
            return Present_wait_result::timeout;
        }
        case VK_ERROR_SURFACE_LOST_KHR:
        case VK_ERROR_DEVICE_LOST: {
            // Same handling as present(): mark invalid; the frame loop's
            // acquire path drives recreation.
            log_swapchain->warn(
                "vkWaitForPresentKHR() returned {} {}; marking swapchain invalid",
                static_cast<int32_t>(result), c_str(result)
            );
            m_is_valid = false;
            return Present_wait_result::unsupported;
        }
        default: {
            // VK_ERROR_OUT_OF_DATE_KHR and anything else: the regular
            // acquire/present path handles recreation; the clamp is a no-op.
            return Present_wait_result::unsupported;
        }
    }
}

auto Swapchain_impl::has_depth() const -> bool
{
    return m_swapchain_objects.depth_image != VK_NULL_HANDLE;
}

auto Swapchain_impl::get_depth_image_view() const -> VkImageView
{
    return m_swapchain_objects.depth_view;
}

auto Swapchain_impl::get_vk_depth_format() const -> VkFormat
{
    return m_depth_format;
}

void Swapchain_impl::create_depth_image(const uint32_t width, const uint32_t height)
{
    const VkImageCreateInfo image_create_info{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = m_depth_format,
        .extent                = {width, height, 1},
        .mipLevels             = 1,
        .arrayLayers           = 1,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VmaAllocationCreateInfo allocation_create_info{
        .flags          = 0,
        .usage          = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .preferredFlags = 0,
        .memoryTypeBits = 0,
        .pool           = VK_NULL_HANDLE,
        .pUserData      = nullptr,
        .priority       = 0.0f
    };

    VmaAllocator& allocator = m_device_impl.get_allocator();
    VkResult result = vmaCreateImage(
        allocator,
        &image_create_info,
        &allocation_create_info,
        &m_swapchain_objects.depth_image,
        &m_swapchain_objects.depth_allocation,
        nullptr
    );
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vmaCreateImage() for swapchain depth failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    vmaSetAllocationName(allocator, m_swapchain_objects.depth_allocation, "Swapchain depth image");
    m_device_impl.set_debug_label(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_swapchain_objects.depth_image), "Swapchain depth image");

    const VkImageViewCreateInfo view_create_info{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = m_swapchain_objects.depth_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = m_depth_format,
        .components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    result = vkCreateImageView(vulkan_device, &view_create_info, nullptr, &m_swapchain_objects.depth_view);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateImageView() for swapchain depth failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_device_impl.set_debug_label(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_swapchain_objects.depth_view), "Swapchain depth image view");
}

void Swapchain_impl::destroy_depth_image(Swapchain_objects& objects)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    if (objects.depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan_device, objects.depth_view, nullptr);
        objects.depth_view = VK_NULL_HANDLE;
    }
    if (objects.depth_image != VK_NULL_HANDLE) {
        VmaAllocator& allocator = m_device_impl.get_allocator();
        vmaDestroyImage(allocator, objects.depth_image, objects.depth_allocation);
        objects.depth_image      = VK_NULL_HANDLE;
        objects.depth_allocation = VK_NULL_HANDLE;
    }
}

auto Swapchain_impl::has_stencil() const -> bool
{
    return false; // TODO
}

auto Swapchain_impl::get_color_format() const -> erhe::dataformat::Format
{
    return to_erhe(m_surface_impl.get_surface_format().format);
}

auto Swapchain_impl::get_depth_format() const -> erhe::dataformat::Format
{
    if (m_depth_format != VK_FORMAT_UNDEFINED) {
        return to_erhe(m_depth_format);
    }
    return erhe::dataformat::Format::format_undefined;
}

} // namespace erhe::graphics
