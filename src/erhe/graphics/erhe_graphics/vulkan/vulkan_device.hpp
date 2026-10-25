#pragma once

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/ring_buffer_pool.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/vulkan/vulkan_pipeline_cache_file.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_time/sleep.hpp"

#include "volk.h"
// vma forward declaration
VK_DEFINE_HANDLE(VmaAllocator)

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {

class Clock_monitor;

class Command_buffer;
class Command_buffer_impl;
class Gpu_timer_impl;

class Instance_layers
{
public:
    bool m_VK_LAYER_AMD_switchable_graphics {false};
    bool m_VK_LAYER_OBS_HOOK                {false};
    bool m_VK_LAYER_RENDERDOC_Capture       {false};
    bool m_VK_LAYER_LUNARG_api_dump         {false};
    bool m_VK_LAYER_LUNARG_gfxreconstruct   {false};
    bool m_VK_LAYER_KHRONOS_synchronization2{false};
    bool m_VK_LAYER_KHRONOS_validation      {false};
    bool m_VK_LAYER_LUNARG_monitor          {false};
    bool m_VK_LAYER_LUNARG_screenshot       {false};
    bool m_VK_LAYER_KHRONOS_profiles        {false};
    bool m_VK_LAYER_KHRONOS_shader_object   {false};
    bool m_VK_LAYER_LUNARG_crash_diagnostic {false};
};
class Instance_extensions
{
public:
    bool m_VK_KHR_get_physical_device_properties2{false};
    bool m_VK_KHR_get_surface_capabilities2      {false};
    bool m_VK_KHR_surface                        {false};
    bool m_VK_KHR_surface_maintenance1           {false};
    bool m_VK_EXT_surface_maintenance1           {false};
    bool m_VK_KHR_win32_surface                  {false};
    bool m_VK_EXT_debug_report                   {false};
    bool m_VK_EXT_debug_utils                    {false};
    bool m_VK_EXT_swapchain_colorspace           {false};
};
class Device_extensions
{
public:
    bool m_VK_KHR_swapchain                     {false};
    bool m_VK_EXT_swapchain_maintenance1        {false};
    bool m_VK_KHR_swapchain_maintenance1        {false};
    bool m_VK_KHR_present_mode_fifo_latest_ready{false};
    bool m_VK_EXT_present_mode_fifo_latest_ready{false};
    bool m_VK_KHR_present_id                    {false};
    bool m_VK_KHR_present_wait                  {false};
    bool m_VK_KHR_present_id2                   {false};
    bool m_VK_KHR_shader_relaxed_extended_instruction{false};
    bool m_VK_KHR_present_wait2                 {false};
    bool m_VK_EXT_present_timing                {false};
    bool m_VK_GOOGLE_display_timing             {false};
    bool m_VK_EXT_full_screen_exclusive         {false};
    bool m_VK_KHR_calibrated_timestamps         {false};
    bool m_VK_EXT_calibrated_timestamps         {false};
    bool m_VK_EXT_device_address_binding_report {false}; // see Device_impl::m_report_address_binding
    bool m_VK_KHR_load_store_op_none            {false};
    bool m_VK_EXT_load_store_op_none            {false};
    bool m_VK_KHR_push_descriptor               {false};
    bool m_VK_KHR_draw_indirect_count           {false};
    bool m_VK_KHR_portability_subset            {false};
    bool m_VK_EXT_fragment_density_map          {false};
    bool m_VK_EXT_fragment_density_map2         {false};
    bool m_VK_KHR_acceleration_structure        {false};
    bool m_VK_KHR_ray_query                     {false};
    bool m_VK_KHR_deferred_host_operations      {false};
    bool m_VK_KHR_ray_tracing_position_fetch    {false};
    bool m_VK_EXT_conservative_rasterization    {false};
    bool m_VK_EXT_memory_budget                 {false};
    bool m_VK_KHR_device_fault                  {false};
    bool m_VK_EXT_device_fault                  {false};
};
class Capabilities
{
public:
    bool m_present_mode_fifo_latest_ready{false};
    bool m_surface_capabilities2         {false};
    bool m_surface_maintenance1          {false};
    bool m_swapchain_maintenance1        {false};

    // Frame pacing capabilities (doc/frame_pacing/capability_tiers.md).
    // Feature-confirmed, not merely extension-present.
    bool m_present_id           {false};
    bool m_present_wait         {false};
    bool m_present_timing       {false};
    // Device features for the FR3 target-time request modes (step P2.3).
    // The surface can still veto either mode per swapchain
    // (VkPresentTimingSurfaceCapabilitiesEXT, see Swapchain_impl).
    bool m_present_at_absolute_time{false};
    bool m_present_at_relative_time{false};
    bool m_calibrated_timestamps{false};
    // VK_GOOGLE_display_timing (MoltenVK): desiredPresentTime as the FR3
    // actuator and actualPresentTime as the feedback, in the platform's
    // presentation clock (Swapchain_impl::display_timing_to_seconds). The
    // tier W alternative to m_present_timing.
    bool m_display_timing       {false};
    // Tier W: all of the above available; the frame pacer may run.
    // Anything less is off mode (FR6: no middle tier).
    bool m_frame_pacing_tier_w  {false};
};

class Frame_begin_info;
class Frame_end_info;
class Device_sync_pool;
class Render_pass_impl;
class Ring_buffer;
class Surface_impl;
class Swapchain;

class Device_frame_in_flight
{
public:
    VkFence         submit_fence  {VK_NULL_HANDLE};
    // Legacy single-pool fields used by the existing begin_frame/end_frame
    // single-cb-per-frame path. To be removed once that path is migrated
    // off Device_frame_in_flight onto the new per-thread pool model.
    VkCommandPool   command_pool  {VK_NULL_HANDLE};
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
};

// One Vulkan VkCommandPool plus the list of Command_buffer instances
// allocated from it. Owned by Device_impl (one Per_thread_command_pool
// per (frame_in_flight, thread_slot) pair). The pool is created with
// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT and reset wholesale when the
// owning frame-in-flight slot's previous use has retired on the GPU.
//
// VkCommandBuffer handles are reused across cycles: vkResetCommandPool()
// returns every allocated cb to the initial state but keeps it owned by
// the pool, so allocating a fresh handle each frame would grow the pool
// without bound (handles are never freed on reset; the driver also keeps
// each live cb's backing command-stream/upload buffers). vk_command_buffers
// holds the handles allocated so far; next_handle_index counts how many
// have been handed out in the current cycle and is reset to 0 together
// with the pool.
class Per_thread_command_pool
{
public:
    VkCommandPool                                command_pool{VK_NULL_HANDLE};
    std::vector<std::unique_ptr<Command_buffer>> allocated_command_buffers;
    std::vector<VkCommandBuffer>                 vk_command_buffers;
    std::size_t                                  next_handle_index{0};
};

// Device-frame lifecycle state of one frame-in-flight slot (OpenXR-style
// three phases, plus an optional nested swapchain-frame layer). Each slot
// has its own state, so slot A may be recording on one thread while slot B
// is waited on another. wait_for_frame_start() (the pacing half of the
// frame start) does not touch this state.
//
//   idle               --acquire_frame_slot(F)--> waited
//   waited             --begin_frame()-------> recording
//   recording          --end_frame(F)--------> idle
//   waited             --end_frame(F)--------> idle
//   in_swapchain_frame --end_frame(F)--------> idle
enum class Device_frame_state : uint8_t
{
    idle,
    waited,
    recording,
    in_swapchain_frame
};

// The state of one frame-in-flight slot and the id of the frame that holds
// it (valid while state != idle).
class Device_frame_slot
{
public:
    Device_frame_state state   {Device_frame_state::idle};
    uint64_t           frame_id{0};
};

[[nodiscard]] auto c_str(Device_frame_state state) -> const char*;

class Device;

// Thread safety (doc/erhe/graphics.md "Thread safety" has the full table).
// With frame pipelining the main role prepares frame N+1 (M2: uploads,
// resource creation and destruction, ring buffer ranges, standalone
// submits) while the render role records, submits and ends frame N (R).
// Every member both roles reach is either
// - guarded by a mutex named next to it, held only for the bookkeeping
//   (m_queue_mutex is the exception: Vulkan requires the queue to be
//   externally synchronized for the whole submit / present / wait idle),
// - split per frame-in-flight slot: the main role touches only the slot of
//   the frame it prepares, the render role only the slot of the frame it
//   renders, and a slot changes hands only through acquire_frame_slot (main)
//   after the render role ended the frame that held it (ordered by the
//   editor's kick / join), or
// - owned by one role, stated next to the member.
class Device_impl final
{
public:
    Device_impl(
        Device&                         device,
        const Surface_create_info&      surface_create_info,
        const Graphics_config&          graphics_config,
        const Vulkan_external_creators* vulkan_external_creators = nullptr
    );
    Device_impl   (const Device_impl&) = delete;
    void operator=(const Device_impl&) = delete;
    Device_impl   (Device_impl&&)      = delete;
    void operator=(Device_impl&&)      = delete;
    ~Device_impl  () noexcept;

    // The two halves of Device::wait_frame() (doc/erhe/graphics.md "Frame
    // lifecycle"). wait_for_frame_start: GPU clock calibration and frame
    // bracket harvest for the frame being started; reads no frame-slot
    // state. acquire_frame_slot: the order rule, the timeline wait and the
    // pool resets for the slot of frame_id.
    // Roles: wait_for_frame_start and acquire_frame_slot are main role
    // (M1 / M2), begin_frame and end_frame the render role (R); each asserts
    // its thread through erhe::utility::Thread_roles (no-op when the roles
    // are unset: init, tests). acquire_frame_slot reads m_render_frame_id
    // under m_frame_id_mutex; begin_frame / end_frame touch only the slot of
    // the frame they render.
                  void wait_for_frame_start(uint64_t frame_id);
    [[nodiscard]] auto acquire_frame_slot  (uint64_t frame_id) -> bool;
    [[nodiscard]] auto begin_frame() -> bool;
    // end_frame(): ends the oldest open frame; end_frame(frame_id) verifies
    // frame_id is that frame.
    [[nodiscard]] auto end_frame  () -> bool;
    [[nodiscard]] auto end_frame  (uint64_t frame_id) -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    // vkDeviceWaitIdle, then completes every frame ended before the wait
    // (ring buffer ranges, completion handlers); when no frame is open it
    // also runs the handlers of frames that never ended. The handlers of an
    // open frame stay queued: that frame's command buffers may still be
    // recording (the other role's frame, or this role's own unsubmitted
    // upload command buffer).
    void               wait_idle            ();
    void               clear_render_pipeline_cache();
    // vkDeviceWaitIdle under m_queue_mutex (the call requires every queue of
    // the device to be externally synchronized). For the backend's own
    // drain points (swapchain recreation, the emulated swapchain capture,
    // destruction); wait_idle() also completes the ended frames.
    [[nodiscard]] auto device_wait_idle() -> VkResult;
    // Serializes host access to the graphics and present queues: every
    // vkQueueSubmit*, vkQueuePresentKHR, queue debug label and
    // vkDeviceWaitIdle of the backend runs under it.
    [[nodiscard]] auto get_queue_mutex() -> std::mutex&;
    [[nodiscard]] auto recreate_surface_for_new_window() -> bool;
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    void start_frame_capture();
    void end_frame_capture  ();

    // The device of the process (single device).
    [[nodiscard]] static auto get_device_impl            () -> Device_impl*;
    // Allocate a fresh primary VkCommandBuffer from the pool owned by
    // (frame-in-flight slot of get_frame_index(), thread_slot), wrap it in a new
    // Command_buffer that lives in the pool, and return that
    // Command_buffer by reference. The returned cb is in the initial
    // state -- the caller must call begin() on it before recording. Its
    // lifetime is tied to the pool: vkResetCommandPool runs when the
    // frame-in-flight slot's submit fence reports completion, freeing
    // every cb that was allocated from the pool that cycle. Verifies the
    // calling role's frame is open (its slot waited or recording, held by
    // that frame) and stamps the frame id into the cb.
    [[nodiscard]] auto get_command_buffer                (unsigned int thread_slot) -> Command_buffer&;

    // Both roles submit (the render role its frame, the main role standalone
    // work in M2); the vkQueueSubmit2 and the implicit present run under
    // m_queue_mutex, the fence wait of submit_command_buffer_and_wait does not.
    void submit_command_buffers   (std::span<Command_buffer* const> command_buffers);
    void submit_command_buffer_and_wait(Command_buffer& command_buffer);
    // Any thread. The handler runs on the thread that observes the
    // completion of the calling role's current frame (get_frame_index()):
    // update_frame_completion (render role) or wait_idle.
    void add_completion_handler   (std::function<void(Device_impl&)> callback);
    void on_thread_enter          ();
    // No per-context resources in the Vulkan backend; see Device::Device.
    void create_per_context_resources() {}

    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (Command_buffer& init_command_buffer, const erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto allocate_upload_staging            (Buffer_target buffer_target, std::size_t byte_count, std::size_t capacity_byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto get_upload_staging_usage           () const -> Upload_staging_usage;
    [[nodiscard]] auto make_blit_command_encoder          (Command_buffer& command_buffer) -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder       (Command_buffer& command_buffer) -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder        (Command_buffer& command_buffer) -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties              (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto probe_image_format_support         (erhe::dataformat::Format format, uint64_t usage_mask) const -> bool;
    [[nodiscard]] auto get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>;
                  void sort_depth_stencil_formats         (std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const;
    [[nodiscard]] auto choose_depth_stencil_format        (const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format;
    [[nodiscard]] auto choose_depth_stencil_format        (unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor                 () -> Shader_monitor&;
    [[nodiscard]] auto get_info                           () const -> const Device_info&;
    [[nodiscard]] auto get_graphics_config                () const -> const Graphics_config&;
    [[nodiscard]] auto is_validation_layer_enabled        () const -> bool;
    [[nodiscard]] auto get_memory_budget                  () const -> Memory_budget;
    [[nodiscard]] auto get_debug_state                    () const -> Device_debug_state;
    [[nodiscard]] auto get_allocator                      () -> VmaAllocator&;
    // Allocator to use for a buffer with the given usage. Buffers that carry
    // Buffer_usage::shader_device_address must come from an allocator created
    // with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; everything else
    // must not, because that flag is allocator-wide - VMA stamps
    // VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT onto every block it allocates
    // (vk_mem_alloc.h, canContainBufferWithDeviceAddress), which would request
    // device addresses for texture and plain-buffer memory that never needs
    // one. Falls back to the main allocator when the device-address allocator
    // does not exist (ray tracing disabled: no such buffer can be created).
    [[nodiscard]] auto get_allocator_for_buffer_usage     (Buffer_usage usage) -> VmaAllocator&;
    [[nodiscard]] auto get_context_window                 () const -> erhe::window::Context_window*;

    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const char* label);
    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const std::string& label);

    [[nodiscard]] auto get_sync_pool() -> Device_sync_pool&;

    // Frame time records (implementation plan step P0.2): storage lives on
    // the backend-neutral Device; this forwards for backend-internal sites.
    [[nodiscard]] auto get_frame_time_recorder() -> erhe::frame_pacing::Frame_time_recorder&;

    // Present-wait clamp (frame pacing FR5, step P2.2): routed to the
    // window surface's swapchain; unsupported when headless / no swapchain.
    [[nodiscard]] auto wait_for_displayed_frame(std::int64_t frame_id, uint64_t timeout_ns) -> Present_wait_result;

    // Target present time (frame pacing FR3, step P2.3): stored here, read
    // by Swapchain_impl::present_image for the matching frame id. Reference-
    // clock seconds; 0.0 = no request. Render role only: set in R before the
    // frame's submit, read by that submit (asserted).
    void               set_present_target_time(std::int64_t frame_id, double target_time_seconds, double hold_until_seconds);
    [[nodiscard]] auto get_present_target_time(std::int64_t frame_id) const -> double;

    // Resolved frame pacing tier (P4.2): capability probes + the
    // frame_pacing_tier graphics config, resolved in the constructor BEFORE
    // the surface picks its present mode (the tier owns that decision).
    [[nodiscard]] auto get_frame_pacing_tier() const -> Frame_pacing_tier;
    [[nodiscard]] auto is_present_target_time_honored() const -> bool;

    // GPU timer infrastructure. Backed by a single shared VkQueryPool sized
    // for s_max_gpu_timers timestamps per frame in flight. Slots are
    // allocated by Gpu_timer_impl and retired by its destructor: the timer
    // is unhooked at once (no result is stored into it after that) and the
    // slot is released when the frame current at the destruction completes,
    // so the timestamps a frame in flight already recorded never land in a
    // slot a new timer was given. Results are retrieved in acquire_frame_slot
    // after the timeline wait guarantees the slice has completed on the GPU.
    // Every member of the timer state is guarded by m_gpu_timer_mutex. See
    // vulkan_gpu_timer.cpp for details.
    static constexpr std::size_t s_max_gpu_timers = 128;
    [[nodiscard]] auto allocate_gpu_timer_slot       (Gpu_timer_impl* timer) -> int;
                  void retire_gpu_timer_slot        (int slot);
                  void release_gpu_timer_slot       (int slot);
    // frame_id is the frame of the cb recorded into (Command_buffer_impl::get_frame_id);
    // it selects the query pool slice.
                  void record_gpu_timer_begin_query (VkCommandBuffer cb, uint64_t frame_id, int slot);
                  void record_gpu_timer_end_query   (VkCommandBuffer cb, uint64_t frame_id, int slot);
                  void maybe_reset_gpu_timer_slice  (VkCommandBuffer cb, uint64_t frame_id);
    [[nodiscard]] auto get_gpu_timer_timestamp_period() const -> double;
    [[nodiscard]] auto are_gpu_timers_supported      () const -> bool;

    // Frame-spanning GPU timestamp bracket (implementation plan step P0.3):
    // one begin/end timestamp pair per frame, host-reset in
    // acquire_frame_slot, polled non-blocking by wait_for_frame_start,
    // calibrated into the reference clock.
    // The begin timestamp is recorded at the vkBeginCommandBuffer of the
    // frame's first command buffer; the end timestamp is recorded into the
    // device-owned frame-end command buffer, which rides on the frame's
    // last work submit: the presenting submit_command_buffers when a cb
    // engaged a swapchain (queued BEFORE the present-request holdback and
    // vkQueuePresentKHR, which block the submitting thread for up to N
    // refresh periods), else end_frame's timeline-signal submit. The
    // bracket thus measures the frame's GPU execution span, which is what
    // the frame pacer's gpu_duration input must be
    // (doc/frame_pacing/inputs.md section 3.3): a bracket end queued after
    // the holdback reads back the holdback as GPU time, and the pacer
    // raises its cadence to fit the time its own holdback produced.
    // record_frame_bracket_end returns that recorded cb, or VK_NULL_HANDLE
    // when the bracket is off, no cb began this frame, or the end was
    // already recorded for this frame.
    // Display refresh init calibration (vulkan_device_display_refresh_calibration.cpp):
    // a dedicated clear-only swapchain on the surface, run at device init
    // before the application's swapchain exists; feeds the device's
    // Display_refresh_resolver.
    void               run_display_refresh_calibration(Surface_impl& surface_impl);
    // Present calibration (vulkan_device_present_calibration.cpp, the
    // algorithm in present_calibration_runner.cpp): tier W through
    // VK_GOOGLE_display_timing only (is_present_target_time_honored()), on a
    // dedicated clear-only swapchain at device init after the display refresh
    // calibration, or adopts the cached entry (Device::begin_present_calibration);
    // stores the result in Device::get_present_calibration().
    void               run_present_calibration(Surface_impl& surface_impl);
    // Seconds before the target present time to request it with
    // VK_GOOGLE_display_timing: the calibrated request offset, empty without
    // one (Device::get_present_request_offset).
    [[nodiscard]] auto get_present_request_offset() const -> std::optional<double>;
    void               record_frame_bracket_begin   (VkCommandBuffer cb, uint64_t frame_id);
    [[nodiscard]] auto record_frame_bracket_end     (uint64_t frame_id) -> VkCommandBuffer;
    // The GPU idle gap between a frame's first submit and its presenting
    // submit (the render thread records the frame command buffer after the
    // upload command buffer was submitted; doc/frame_pacing/inputs.md
    // section 3.3): record_frame_bracket_gap_begin returns a cb to append to
    // the frame's first non-presenting submit, record_frame_bracket_gap_end
    // one to prepend to its presenting submit once the gap began; each
    // VK_NULL_HANDLE when there is nothing to record.
    [[nodiscard]] auto record_frame_bracket_gap_begin(uint64_t frame_id) -> VkCommandBuffer;
    [[nodiscard]] auto record_frame_bracket_gap_end  (uint64_t frame_id) -> VkCommandBuffer;
    [[nodiscard]] auto record_frame_bracket_timestamp(VkCommandBuffer cb, uint64_t frame_id, uint32_t query) -> VkCommandBuffer;
    void               reset_frame_end_command_pool (std::size_t in_flight_slot);
    void               poll_frame_bracket_results   (uint64_t started_frame_id);
    void               update_gpu_calibration       (uint64_t started_frame_id);
    [[nodiscard]] auto gpu_ticks_to_reference_seconds(uint64_t ticks) const -> double;

    // Calibrated host time domains (VK_KHR_calibrated_timestamps). Which host
    // domains a driver offers is driver-specific - MoltenVK and KosmicKrisp
    // offer CLOCK_MONOTONIC_RAW but NOT CLOCK_MONOTONIC - so the domain is
    // picked from what the device advertises, never assumed by platform.
    // select_calibrated_host_time_domain() runs during device init, before
    // the calibrated timestamps capability is resolved.
    void               select_calibrated_host_time_domain();
    // The host domain paired with VK_TIME_DOMAIN_DEVICE_KHR in every
    // calibration call; VK_TIME_DOMAIN_DEVICE_KHR itself means "no usable
    // host domain", which turns the calibrated timestamps capability off.
    [[nodiscard]] auto get_calibrated_host_time_domain() const -> VkTimeDomainKHR;
    // True for a host domain this device advertises and this platform can
    // read, i.e. one host_domain_value_to_seconds() can convert.
    [[nodiscard]] auto is_host_time_domain(VkTimeDomainKHR domain) const -> bool;
    // Converts a value in the given host time domain (QPC ticks for
    // QUERY_PERFORMANCE_COUNTER, nanoseconds for the monotonic domains) to
    // reference-clock seconds; 0.0 for a domain that is not a usable host
    // domain. Used for present-timing feedback (step P0.4).
    [[nodiscard]] auto host_domain_value_to_seconds(VkTimeDomainKHR domain, uint64_t value) const -> double;
    // Inverse: reference-clock seconds to a value in the given host domain;
    // 0 for a domain that is not a usable host domain. Used for the FR3
    // absolute target present time (step P2.3).
    [[nodiscard]] auto reference_seconds_to_host_domain_value(VkTimeDomainKHR domain, double seconds) const -> uint64_t;
    // Same conversions for the selected calibrated host domain.
    [[nodiscard]] auto host_calibrated_value_to_seconds(uint64_t value) const -> double;
    [[nodiscard]] auto reference_seconds_to_host_calibrated_value(double seconds) const -> uint64_t;

    [[nodiscard]] auto get_device                       () -> Device&;
    [[nodiscard]] auto get_surface                      () -> Surface*;
    // Reads the most recently composited frame to host memory as tightly
    // packed 8-bit RGBA. Headless (emulated swapchain): synchronous readback.
    // Windowed (real WSI swapchain): returns a previously armed capture
    // (request_frame_capture) once a frame has recorded it; returns false
    // when nothing has been captured yet.
    [[nodiscard]] auto capture_last_frame               (int& out_width, int& out_height, erhe::dataformat::Format& out_format, std::vector<std::byte>& out_pixels) -> bool;
    // Arms a one-shot capture of the next composited frame on the real WSI
    // swapchain (collected by capture_last_frame on a later call). Returns
    // false when no capture path exists (no surface, or the surface did not
    // grant TRANSFER_SRC usage); true and a no-op when headless (emulated
    // swapchain capture is synchronous).
    [[nodiscard]] auto request_frame_capture            () -> bool;
    [[nodiscard]] auto get_native_handles               () const -> Native_device_handles;
    [[nodiscard]] auto get_vulkan_instance              () -> VkInstance;
    [[nodiscard]] auto get_vulkan_physical_device       () -> VkPhysicalDevice;
    [[nodiscard]] auto get_vulkan_device                () -> VkDevice;
    [[nodiscard]] auto get_graphics_queue_family_index  () const -> uint32_t;
    [[nodiscard]] auto get_present_queue_family_index   () const -> uint32_t;
    [[nodiscard]] auto get_graphics_queue               () const -> VkQueue;
    [[nodiscard]] auto get_present_queue                () const -> VkQueue;
#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE)
    // Tracy Vulkan GPU profiling context (created in vulkan_device_init.cpp,
    // null until then / when GPU timestamps are unsupported). Used by
    // erhe::graphics::Scoped_gpu_zone to open TracyVkZone scopes.
    [[nodiscard]] auto get_tracy_vk_ctx                 () const -> TracyVkCtx { return m_tracy_vk_ctx; }
#endif
    [[nodiscard]] auto get_capabilities                 () const -> const Capabilities&;
    [[nodiscard]] auto get_device_extensions            () const -> const Device_extensions&;
    // VK_KHR_device_fault / VK_EXT_device_fault crash diagnostics. True only
    // when the extension was enabled AND its deviceFault feature is on, i.e.
    // when report_device_fault() can actually produce a report.
    [[nodiscard]] auto has_device_fault_report          () const -> bool;
    // Retrieve and log the driver's fault report(s) for a lost device. Safe
    // (and a cheap no-op) to call when the extension is unavailable or when
    // the driver has no fault to report; call it from every site that
    // observes VK_ERROR_DEVICE_LOST, before aborting. Defined in
    // vulkan_device_debug.cpp.
    void report_device_fault(const char* site);
    [[nodiscard]] auto get_driver_properties            () const -> const VkPhysicalDeviceDriverProperties&;
    [[nodiscard]] auto get_portability_subset_features  () const -> const VkPhysicalDevicePortabilitySubsetFeaturesKHR&;
    [[nodiscard]] auto get_portability_subset_properties() const -> const VkPhysicalDevicePortabilitySubsetPropertiesKHR&;
    [[nodiscard]] auto get_acceleration_structure_properties() const -> const VkPhysicalDeviceAccelerationStructurePropertiesKHR&;
    [[nodiscard]] auto get_memory_type                  (uint32_t memory_type_index) const -> const VkMemoryType&;
    [[nodiscard]] auto get_memory_heap                  (uint32_t memory_heap_index) const -> const VkMemoryHeap&;
    [[nodiscard]] auto get_pipeline_cache               () const -> VkPipelineCache;
    // vkGetPipelineCacheData -> the file of m_pipeline_cache_path (destructor).
    void               write_pipeline_cache            () noexcept;
    [[nodiscard]] auto get_descriptor_set_layout        () const -> VkDescriptorSetLayout;
    [[nodiscard]] auto has_push_descriptor              () const -> bool;
    [[nodiscard]] auto get_texture_set_layout           () const -> VkDescriptorSetLayout;
    // Descriptor count of the texture heap set layout (set 1):
    // max_texture_heap_size bounded by the device's maxPerSetDescriptors.
    [[nodiscard]] auto get_texture_heap_size            () const -> uint32_t;
    // Smallest variable descriptor count a texture heap set is allocated
    // with: 1 where the driver honours variable counts, get_texture_heap_size()
    // where the start-up probe found it refusing small counts (Adreno; see
    // doc/erhe/vulkan_backend.md "Texture heap").
    [[nodiscard]] auto get_texture_heap_variable_count_minimum() const -> uint32_t;
    [[nodiscard]] auto get_cached_pipeline              (std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto create_graphics_pipeline         (const VkGraphicsPipelineCreateInfo& create_info, std::size_t hash) -> VkPipeline;

    // The graphics pipeline map is keyed on raw handle values (pipeline
    // layout, shader modules, render pass). The owner of such a handle calls
    // this before destroying it, so that a new object given the same handle
    // value cannot be served a pipeline built for the old one. The retired
    // pipelines are destroyed once the frames in flight have completed.
    void               retire_pipelines_using          (uint64_t handle);
    [[nodiscard]] auto get_or_create_graphics_pipeline  (const VkGraphicsPipelineCreateInfo& create_info, std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto get_or_create_compatible_render_pass(
        unsigned int                                   color_attachment_count,
        const std::array<erhe::dataformat::Format, 4>& color_attachment_formats,
        erhe::dataformat::Format                       depth_attachment_format,
        erhe::dataformat::Format                       stencil_attachment_format,
        unsigned int                                   sample_count,
        uint32_t                                       view_mask,
        bool                                           fragment_density_map
    ) -> VkRenderPass;
    // Fallback descriptor sets (no VK_KHR_push_descriptor), allocated from
    // the descriptor pool of the frame-in-flight slot of frame_id (the frame
    // of the command buffer being recorded). Both roles allocate, each from
    // its own frame's pool; m_descriptor_pool_mutex serializes the calls
    // (worker threads share the main role's frame). acquire_frame_slot
    // resets the slot's pool after the timeline wait retired its last use.
    [[nodiscard]] auto allocate_descriptor_set          (uint64_t frame_id) -> VkDescriptorSet;

    [[nodiscard]] auto debug_report_callback(
        VkDebugReportFlagsEXT      flags,
        VkDebugReportObjectTypeEXT object_type,
        uint64_t                   object,
        size_t                     location,
        int32_t                    message_code,
        const char*                layer_prefix,
        const char*                message
    ) -> VkBool32;

    [[nodiscard]] auto debug_utils_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
        VkDebugUtilsMessageTypeFlagsEXT             message_types,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data
    ) -> VkBool32;

    [[nodiscard]] auto get_number_of_frames_in_flight() const -> size_t;
    // The frame of the calling role (Device::get_frame_index()): the render
    // role gets m_render_frame_id (which only it writes, so it reads it
    // without a lock), every other caller get_prepare_frame_id() (under
    // m_frame_id_mutex).
    [[nodiscard]] auto get_frame_index               () const -> uint64_t;
    // The frame-in-flight slot of a frame id (frame_id % frames in flight).
    // The swapchains index their per-slot state with it, from the frame id
    // of the command buffer that drives them.
    [[nodiscard]] auto get_frame_slot_index          (uint64_t frame_id) const -> std::size_t;

    // True when the GPU has fully retired all work submitted for the given
    // frame index -- resources last referenced by that frame's command
    // buffers can be safely reused or destroyed. Any thread: reads
    // m_latest_completed_frame under m_frame_id_mutex.
    [[nodiscard]] auto is_frame_completed            (uint64_t frame) const -> bool;
    // True once end_frame() ended the frame (every command buffer of it is
    // submitted; the GPU may still run it). Any thread: reads
    // m_render_frame_id under m_frame_id_mutex, so what the ending role
    // wrote before it ended the frame is visible to the caller.
    [[nodiscard]] auto is_frame_ended                (uint64_t frame) const -> bool;

    // Per-frame-in-flight submission resources used by Swapchain_impl. Device_impl
    // owns the command pools and their command buffers; the submit_fence slot is
    // filled/recycled by Swapchain_impl (it owns the fence pool).
    [[nodiscard]] auto get_device_frame_in_flight        (size_t index) -> Device_frame_in_flight&;
    void               ensure_device_frame_command_buffer(size_t index);
    void               reset_device_frame_command_pool   (size_t index);
    void               ensure_device_frame_slot          (size_t index);

private:
    static constexpr size_t       s_number_of_frames_in_flight = 2;
    static constexpr unsigned int s_number_of_thread_slots     = 8;

    // Signals the device timeline at frame_id (the frame being ended) and
    // advances m_render_frame_id (render role).
    void update_frame_completion(uint64_t frame_id);

    // Runs frame_completed for every frame up to and including
    // last_completed_frame not completed yet, under m_completion_run_mutex
    // (the render role's update_frame_completion and a main role wait_idle
    // may both get here).
    void complete_frames_through(uint64_t last_completed_frame);

    // Runs and removes every completion handler (destruction, and wait_idle
    // when no frame is open).
    void drain_completion_handlers();

    // Resets the fallback descriptor pool of a frame-in-flight slot
    // (acquire_frame_slot, after the timeline wait).
    void reset_descriptor_pool(std::size_t in_flight_slot);

    // The frame the main role works on: the newest frame started by
    // wait_for_frame_start() or acquired, otherwise the next frame to be
    // acquired. With frame pipelining the main role starts frame N+1 while
    // the render role still renders frame N: from the start on, the main
    // role's frame is N+1, as it is in the serial schedule once frame N
    // ended. Reads the frame ids under m_frame_id_mutex.
    [[nodiscard]] auto get_prepare_frame_id() const -> uint64_t;

    // Single point of truth for transitioning a frame slot's state. Every
    // assignment to m_frame_slots[].state goes through here so that one
    // trace site captures all transitions along with the frame id and
    // slot -- critical for diagnosing frame-lifecycle / slot-desync bugs.
    // The site parameter is a short literal naming the caller (e.g.
    // "acquire_frame_slot", "begin_frame", "end_frame"); it shows up in
    // the trace and makes the log skimmable.
    void set_state(uint64_t frame_id, Device_frame_state new_state, const char* site);

    // True when this physical device would support GPU ray tracing but it
    // must be left disabled because RenderDoc capture is active.
    //
    // Narrowly scoped to the configuration where it was actually measured:
    // AMD on Windows. With RenderDoc's capture layer loaded, that driver
    // faults on a wild GPU virtual address (a single unmapped 4 KiB page in
    // the device-address range, reported as an Invalid Read or Invalid Write
    // by VK_EXT_device_fault) shortly after a texture-heavy load. Bisected to
    // VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT on allocations: enabling the
    // bufferDeviceAddress feature alone is harmless, and the ray tracing
    // extensions alone are harmless, but allocating device-address memory is
    // not. erhe cannot avoid that flag and still build acceleration
    // structures - vkCmdBuildAccelerationStructuresKHR consumes device
    // addresses for scratch, geometry and instance data, and
    // VUID-vkBindBufferMemory-bufferDeviceAddress-03339 then requires the
    // backing memory to carry the flag.
    //
    // AMD on Linux and NVIDIA on Windows were both verified unaffected, so
    // this deliberately does not fire for them. The Metal backend has the
    // same shape of workaround for acceleration structure encoders crashing
    // inside GPUToolsCapture (metal_device.cpp), and reports it through the
    // same Device_info::ray_query_disabled_by_capture_layer flag.
    [[nodiscard]] static auto is_ray_tracing_blocked_by_capture_layer(
        VkPhysicalDevice vulkan_physical_device,
        bool             renderdoc_capture_support
    ) -> bool;
    [[nodiscard]] static auto get_physical_device_score(VkPhysicalDevice vulkan_physical_device, Surface_impl* surface_impl, bool disable_ray_tracing) -> float;
    [[nodiscard]] static auto query_device_queue_family_indices(
        VkPhysicalDevice vulkan_physical_device,
        Surface_impl*    surface_impl,
        uint32_t*        graphics_queue_family_index,
        uint32_t*        present_queue_family_index
    ) -> bool;
    static auto query_device_extensions(
        VkPhysicalDevice          vulkan_physical_device,
        Device_extensions&        device_extensions_out,
        std::vector<const char*>* device_extensions_c_str,
        bool                      headless,
        bool                      disable_ray_tracing
    ) -> float;
    [[nodiscard]] auto choose_physical_device(Surface_impl* surface_impl, std::vector<const char*>& device_extensions_c_str) -> bool;

    // Retires one completed frame: the ring buffer ranges, then the
    // completion handlers of that frame (taken out of the list under
    // m_completion_handler_mutex, run without it, so a handler may destroy
    // objects that add handlers). Called with m_completion_run_mutex held.
    void frame_completed(uint64_t frame);

    erhe::window::Context_window* m_context_window{nullptr};
    Device&                       m_device;
    Graphics_config               m_graphics_config;
    Shader_monitor                m_shader_monitor;
    Device_info                   m_info;
    class Completion_handler
    {
    public:
        uint64_t                          frame_number;
        std::function<void(Device_impl&)> callback;
    };
    // m_completion_handlers: any thread adds (add_completion_handler), the
    // completing thread takes; guarded by m_completion_handler_mutex.
    // m_completion_run_mutex serializes completing frames (frame_completed,
    // m_completion_handler_scratch); lock order: m_completion_run_mutex,
    // then the ring buffer pool, then m_completion_handler_mutex /
    // m_frame_id_mutex. Never held across a GPU wait.
    std::mutex                      m_completion_run_mutex;
    mutable std::mutex              m_completion_handler_mutex;
    std::vector<Completion_handler> m_completion_handlers;
    std::vector<Completion_handler> m_completion_handler_scratch;

    // Locked internally (Device_sync_pool::m_mutex): the implicit semaphore
    // and fence of every command buffer come from it on both roles.
    std::unique_ptr<Device_sync_pool>          m_sync_pool;

    VkInstance               m_vulkan_instance            {VK_NULL_HANDLE};
    VkPhysicalDevice         m_vulkan_physical_device     {VK_NULL_HANDLE};
    VkDevice                 m_vulkan_device              {VK_NULL_HANDLE};
    VmaAllocator             m_vma_allocator              {VK_NULL_HANDLE};
    // Second allocator, created only when ray tracing is enabled, carrying
    // VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT. Used exclusively for
    // buffers with Buffer_usage::shader_device_address. See
    // get_allocator_for_buffer_usage().
    VmaAllocator             m_vma_device_address_allocator{VK_NULL_HANDLE};

    // Set during physical device selection; drives
    // Device_info::ray_query_disabled_by_capture_layer so the editor can tell
    // the user why ray tracing is unavailable.
    bool                     m_ray_tracing_blocked_by_capture_layer{false};
    VkDebugReportCallbackEXT m_debug_report_callback      {VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_utils_messenger      {VK_NULL_HANDLE};
    std::unique_ptr<Surface> m_surface                    {};
    VkQueue                  m_vulkan_graphics_queue      {VK_NULL_HANDLE};
    VkQueue                  m_vulkan_present_queue       {VK_NULL_HANDLE};
    uint32_t                 m_graphics_queue_family_index{0};
    uint32_t                 m_present_queue_family_index {0};

    // Serializes host access to m_vulkan_graphics_queue and
    // m_vulkan_present_queue (get_queue_mutex()).
    std::mutex               m_queue_mutex;

    // Per-frame-in-flight submission resources consumed by Swapchain_impl.
    // Device_impl owns the VkCommandPool + VkCommandBuffer; Swapchain_impl
    // populates/recycles submit_fence from its own fence pool. Render role
    // only (the swapchain belongs to the render role), per slot.
    std::array<Device_frame_in_flight, s_number_of_frames_in_flight> m_device_submit_history{};

    // [frame_in_flight_slot][thread_slot] command pools + their
    // currently-allocated Command_buffers. Split per frame slot (each role
    // gets command buffers of its own frame) and per thread slot (one
    // recording thread per thread slot of a frame: the caller's contract). Pools are created once at
    // Device_impl construction with VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    // and torn down at destruction. Each frame the per-slot pools are
    // wholesale-reset (vkResetCommandPool + clear the
    // allocated_command_buffers vector) once the slot's submit fence
    // reports GPU completion.
    std::array<
        std::array<Per_thread_command_pool, s_number_of_thread_slots>,
        s_number_of_frames_in_flight
    > m_command_pools{};

    // Device-internal per-frame-in-flight command pool + its single primary
    // cb, separate from the user thread slots: end_frame records the frame
    // bracket end timestamp into it and submits it with the frame's
    // timeline signal. Created once at construction, reset by acquire_frame_slot
    // together with the per-thread pools (same timeline gate).
    std::array<VkCommandPool,   s_number_of_frames_in_flight> m_frame_end_command_pools  {};
    std::array<VkCommandBuffer, s_number_of_frames_in_flight> m_frame_end_command_buffers{};
    // From the same pool: the frame bracket's gap begin / gap end timestamps.
    std::array<VkCommandBuffer, s_number_of_frames_in_flight> m_frame_gap_begin_command_buffers{};
    std::array<VkCommandBuffer, s_number_of_frames_in_flight> m_frame_gap_end_command_buffers{};

    VkSemaphore              m_vulkan_frame_end_semaphore {VK_NULL_HANDLE};

    // Frame ids. m_frame_id_mutex guards every write of the four and every
    // read by a role that does not write the member:
    // - m_render_frame_id: the oldest open frame, the one end_frame() ends
    //   next; when no frame is open, the next frame to be acquired. Written
    //   only by update_frame_completion (the role that ends frames, the
    //   render role); that role reads it without the lock. Frame ids start
    //   at 1.
    // - m_prepare_frame_id: the newest acquired frame (0: none yet). Written
    //   only by acquire_frame_slot (main role). Invariant while frames are
    //   open: m_render_frame_id <= m_prepare_frame_id < m_render_frame_id + N.
    // - m_started_frame_id: the newest frame wait_for_frame_start() started
    //   (0: none yet). Written only by wait_for_frame_start (main role).
    // - m_latest_completed_frame: one past the newest frame the GPU retired
    //   and frame_completed ran for; advanced under m_completion_run_mutex
    //   by complete_frames_through, read by is_frame_completed (any thread).
    mutable std::mutex       m_frame_id_mutex;
    uint64_t                 m_latest_completed_frame     {0}; // GPU
    uint64_t                 m_render_frame_id            {1};
    uint64_t                 m_prepare_frame_id           {0};
    uint64_t                 m_started_frame_id           {0};

    // Target present time request (frame pacing FR3, step P2.3) plus the
    // pacer-computed present-request holdback deadline (claim C15; derived
    // from the tracked grid, deviation 12). Render role only.
    std::int64_t             m_present_target_frame_id    {-1};
    double                   m_present_target_time_seconds{0.0};
    double                   m_present_target_hold_until_seconds{0.0};
    // Present-request holdback timer (claim C15 mitigation).
    erhe::time::Waitable_timer m_present_holdback_timer;

    // Per-frame-in-flight-slot lifecycle state. Drives the assertions in
    // acquire_frame_slot / begin_frame / end_frame / get_command_buffer and
    // backs is_in_swapchain_frame. Per slot: acquire_frame_slot (main role)
    // writes the slot of the frame it acquires, which no open frame holds;
    // begin_frame / end_frame (render role) write the slot of the frame they
    // render; each role reads only the slot of its own frame.
    std::array<Device_frame_slot, s_number_of_frames_in_flight> m_frame_slots{};


    void report_device_fault_khr();
    void report_device_fault_ext();

    // Device fault reporting (VK_KHR_device_fault preferred,
    // VK_EXT_device_fault fallback for the drivers that only ship the EXT).
    // Set at device creation from the extension probe plus the queried
    // deviceFault feature; read by report_device_fault().
    bool                     m_device_fault_report_khr{false};
    bool                     m_device_fault_report_ext{false};

    Instance_layers          m_instance_layers    {};
    Instance_extensions      m_instance_extensions{};
    Device_extensions        m_device_extensions  {};
    Capabilities             m_capabilities       {};
    Frame_pacing_tier        m_frame_pacing_tier  {Frame_pacing_tier::off};

    VkPhysicalDeviceDriverProperties               m_driver_properties{};
    VkPhysicalDeviceDepthStencilResolveProperties  m_depth_stencil_resolve_properties{};
    VkPhysicalDeviceMemoryProperties2              m_memory_properties{};
    // Portability subset features/properties as queried from the physical
    // device. When VK_KHR_portability_subset is NOT advertised we treat the
    // device as fully featured: every feature flag is forced to VK_TRUE and
    // properties are set to values that impose no extra constraints. Vulkan
    // backend classes can therefore consult these structs unconditionally
    // instead of repeating the extension check at every call site.
    VkPhysicalDevicePortabilitySubsetFeaturesKHR   m_portability_subset_features{};
    VkPhysicalDevicePortabilitySubsetPropertiesKHR m_portability_subset_properties{};

    // Acceleration structure limits. Chained into the properties2 query only
    // when VK_KHR_acceleration_structure is advertised; otherwise stays at
    // conservative defaults so call sites can read it unconditionally.
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_acceleration_structure_properties{};

    // Pipeline infrastructure. The pipeline cache is persisted across runs
    // (vulkan_pipeline_cache_file.hpp): created from the file of this
    // device's identity, written back in the destructor.
    Pipeline_cache_identity                       m_pipeline_cache_identity  {};
    std::filesystem::path                         m_pipeline_cache_path      {};
    VkPipelineCache                               m_pipeline_cache           {VK_NULL_HANDLE};
    VkDescriptorSetLayout                         m_descriptor_set_layout    {VK_NULL_HANDLE};
    VkDescriptorSetLayout                         m_texture_set_layout       {VK_NULL_HANDLE};
    uint32_t                                      m_texture_heap_size{0};
    uint32_t                                      m_texture_heap_variable_count_minimum{1};
    // Fallback descriptor pools (no push descriptors), one per frame in
    // flight; see allocate_descriptor_set.
    std::mutex                                    m_descriptor_pool_mutex;
    std::array<VkDescriptorPool, s_number_of_frames_in_flight> m_per_frame_descriptor_pools{};
    class Cached_pipeline
    {
    public:
        VkPipeline              pipeline{VK_NULL_HANDLE};
        std::array<uint64_t, 4> handles {}; // layout, vertex module, fragment module, render pass
    };
    std::mutex                                       m_pipeline_map_mutex;
    std::unordered_map<std::size_t, Cached_pipeline> m_pipeline_map;
    std::mutex                                    m_compatible_render_pass_mutex;
    std::unordered_map<std::size_t, VkRenderPass> m_compatible_render_pass_map;

    // Locked internally, ring buffers per role (ring_buffer_pool.hpp).
    Ring_buffer_pool                          m_ring_buffer_pool{m_device, 2 * 1024 * 1024};

    // GPU timers. m_gpu_timer_query_pool is sized for
    // s_max_gpu_timers * 2 * s_number_of_frames_in_flight queries, laid out
    // as [slice 0 | slice 1] where each slice has 2 queries per timer
    // (begin, end); a slice is the frame-in-flight slot of the recording
    // command buffer's frame. m_gpu_timer_mutex serializes the free list,
    // the timer pointers, the fired bitmaps and the reset flags from any
    // thread that touches a Gpu_timer (ctor/dtor, write_begin/write_end,
    // acquire_frame_slot result-read, which stores results into the timers
    // with the mutex held so a timer being destroyed is never written).
    VkQueryPool m_gpu_timer_query_pool      {VK_NULL_HANDLE};

    // Frame-spanning GPU timestamp bracket state (step P0.3). The query
    // pools and m_frame_bracket_frame_id are main role only (acquire_frame_slot,
    // wait_for_frame_start); m_frame_bracket_begun and m_frame_bracket_ended
    // are per frame-in-flight slot (acquire_frame_slot resets them, the
    // first command buffer begun for the frame sets begun, the presenting
    // submit or end_frame sets ended). One pool of
    // two queries (begin, end) per ring slot: acquire_frame_slot host-resets the
    // slot it is about to reuse, and a host reset is only valid once every
    // submitted command buffer referencing that pool has completed - with
    // one pool per slot that is exactly frame (F - ring), retired long ago,
    // while a single shared pool would still be referenced by the frames in
    // flight (VUID-vkResetQueryPool-firstQuery-02741). Queries 2 and 3 are
    // the gap begin / gap end (record_frame_bracket_gap_begin), written
    // only by frames whose m_frame_bracket_has_gap is set.
    static constexpr std::size_t s_frame_bracket_ring = 16;
    static constexpr uint32_t    s_frame_bracket_query_count = 4;
    std::array<VkQueryPool, s_frame_bracket_ring> m_frame_bracket_query_pools{};
    bool         m_frame_bracket_enabled{false};
    std::array<std::int64_t, s_frame_bracket_ring> m_frame_bracket_frame_id{};
    // Per ring slot: both gap queries were recorded for that frame.
    std::array<bool, s_frame_bracket_ring> m_frame_bracket_has_gap{};
    // Tier W on Apple (MoltenVK): the GPU and CPU clocks per frame
    // (clock_monitor_apple.hpp, claim C25); the frame bracket results reach
    // the recorder through it so a GPU span never arrives without its clocks.
    std::shared_ptr<Clock_monitor> m_clock_monitor;
    // Per frame-in-flight slot: the frame's bracket begin timestamp was
    // recorded (by the first cb of that frame to begin).
    std::array<bool, s_number_of_frames_in_flight> m_frame_bracket_begun{};
    // Per frame-in-flight slot: the frame's bracket end timestamp was
    // recorded (by the presenting submit, else by end_frame).
    std::array<bool, s_number_of_frames_in_flight> m_frame_bracket_ended{};
    // Per frame-in-flight slot: the gap begin timestamp was recorded.
    std::array<bool, s_number_of_frames_in_flight> m_frame_bracket_gap_begun{};
    bool         m_host_query_reset   {false};
    bool         m_report_address_binding{false};
    // GPU clock calibration and host time domain offsets: written by
    // wait_for_frame_start (the main thread in the editor's M1), read by the
    // present-timing conversions on the thread that presents (the render
    // thread), so both sides hold m_time_calibration_mutex. The *_frame
    // throttle fields are touched only by wait_for_frame_start.
    mutable std::mutex m_time_calibration_mutex;
    bool         m_gpu_calibration_valid       {false};
    double       m_gpu_calibration_host_seconds{0.0};
    uint64_t     m_gpu_calibration_device_ticks{0};
    uint64_t     m_gpu_calibration_frame       {0};

    // Host time domains this device advertises and this platform can read
    // (select_calibrated_host_time_domain). offset_seconds maps the domain
    // onto the reference clock (Frame_time_recorder::now()) and is NOT zero
    // in general: on macOS libc++ steady_clock is CLOCK_MONOTONIC_RAW while
    // CLOCK_MONOTONIC also counts time spent asleep; on Linux steady_clock
    // is CLOCK_MONOTONIC and the raw clock is the odd one out. Offsets are
    // re-measured with the GPU calibration, since the gap between two
    // monotonic clocks moves whenever the system sleeps.
    struct Host_time_domain
    {
        VkTimeDomainKHR domain          {VK_TIME_DOMAIN_DEVICE_KHR};
        double          ticks_per_second{1.0};
        double          offset_seconds  {0.0};
    };
    void refresh_host_time_domain_offsets(uint64_t started_frame_id);
    // host_domain_value_to_seconds with m_time_calibration_mutex held.
    [[nodiscard]] auto host_domain_value_to_seconds_locked(VkTimeDomainKHR domain, uint64_t value) const -> double;
    std::vector<Host_time_domain> m_host_time_domains;
    VkTimeDomainKHR m_calibrated_host_time_domain{VK_TIME_DOMAIN_DEVICE_KHR};
    uint64_t        m_host_time_domain_offset_frame{0};
    bool            m_host_time_domain_offsets_valid{false};
    double      m_gpu_timer_timestamp_period{0.0};
    uint64_t    m_gpu_timer_valid_mask      {0};
    bool        m_gpu_timers_supported      {false};
    std::mutex  m_gpu_timer_mutex;
    std::array<Gpu_timer_impl*,       s_max_gpu_timers> m_gpu_timer_by_index{};
    std::array<bool,                  s_max_gpu_timers> m_gpu_timer_slot_used{};
    std::array<std::array<bool, s_max_gpu_timers>, s_number_of_frames_in_flight> m_gpu_timer_fired{};
    std::array<bool, s_number_of_frames_in_flight>                              m_gpu_timer_reset_pending{};
    // The frame id that last recorded timer queries into each slice.
    std::array<uint64_t, s_number_of_frames_in_flight>                          m_gpu_timer_slice_frame_id{};

#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE)
    // Tracy Vulkan GPU profiling context, created in vulkan_device_init.cpp
    // when GPU timestamps are supported and destroyed in ~Device_impl. The
    // per-frame TracyVkCollect is issued from maybe_reset_gpu_timer_slice.
    TracyVkCtx  m_tracy_vk_ctx{nullptr};
#endif

    static Device_impl*  s_device_impl;

    // Optional hooks used when erhe::xr wraps Vulkan creation via
    // XR_KHR_vulkan_enable2. Stored as a raw pointer; lifetime is managed by
    // the caller (typically erhe::xr::Headset) and must outlive Device_impl
    // construction.
    const Vulkan_external_creators* m_external_creators{nullptr};
};

} // namespace erhe::graphics
