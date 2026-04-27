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
        ERHE_VERIFY(frame.present_semaphore == VK_NULL_HANDLE);
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

    //log_swapchain->trace("During the lifetime, {} swapchains were created", m_swapchain_creation_count);
    log_swapchain->debug("Old swapchain count at destruction: {}", m_old_swapchains.size());

    for (Swapchain_cleanup_data& old_swapchain : m_old_swapchains) {
        cleanup_old_swapchain(old_swapchain);
    }

    cleanup_swapchain_objects(m_swapchain_objects);
    if (m_vulkan_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkan_device, m_vulkan_swapchain, nullptr);
    }
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

    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        init_swapchain();
    }

    out_frame_state = Frame_state{};
    setup_frame();

    m_state = Swapchain_frame_state::command_buffer_ready;

    return is_valid();
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
        (result == VK_ERROR_OUT_OF_DATE_KHR)
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

    if ((result == VK_SUBOPTIMAL_KHR) || (result == VK_ERROR_OUT_OF_DATE_KHR)) {
        init_swapchain();
    } else if (result != VK_SUCCESS) {
        m_device_impl.get_device().device_message(
            Message_severity::error,
            fmt::format("presenting frame failed with {} {}", static_cast<int32_t>(result), c_str(result))
        );
        m_state = Swapchain_frame_state::idle;
    }
    m_state = Swapchain_frame_state::image_present;
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
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateSwapchainKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_SWAPCHAIN_KHR,
        reinterpret_cast<uint64_t>(m_vulkan_swapchain),
        fmt::format("Swapchain {}", m_swapchain_serial)
    );

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

auto Swapchain_impl::acquire_next_image(uint32_t* index) -> VkResult
{
    VkDevice                   vulkan_device = m_device_impl.get_vulkan_device();
    Swapchain_frame_in_flight& frame         = m_submit_history[current_slot()];

    // Use a fence to know when acquire is done.  Without VK_EXT_swapchain_maintenance1, this
    // fence is used to infer when the _previous_ present to this image index has finished.
    // There is no need for this with VK_EXT_swapchain_maintenance1.
    VkFence acquire_fence = has_maintenance1() ? VK_NULL_HANDLE : get_fence();

    VkResult result = vkAcquireNextImageKHR(vulkan_device, m_vulkan_swapchain, UINT64_MAX, frame.acquire_semaphore, acquire_fence, index);

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

    log_swapchain->trace(
        "vkQueuePresentKHR image_index={} present_sem=0x{:x} swapchain=0x{:x}",
        index,
        reinterpret_cast<std::uintptr_t>(frame.present_semaphore),
        reinterpret_cast<std::uintptr_t>(m_vulkan_swapchain)
    );
    VkResult result = vkQueuePresentKHR(vulkan_present_queue, &present_info);
    log_swapchain->trace("vkQueuePresentKHR returned {}", static_cast<int32_t>(result));
    if ((result != VK_SUCCESS) && (result != VK_ERROR_OUT_OF_DATE_KHR) && (result != VK_SUBOPTIMAL_KHR)) {
        log_swapchain->critical("vkQueuePresentKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    add_present_to_history(index, present_fence);
    cleanup_present_history();

    return result;
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
