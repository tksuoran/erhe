#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include "volk.h"
// vma forward declaration
VK_DEFINE_HANDLE(VmaAllocator)

#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {

class Command_buffer;
class Command_buffer_impl;

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
    bool m_VK_EXT_device_address_binding_report {false};
    bool m_VK_KHR_load_store_op_none            {false};
    bool m_VK_EXT_load_store_op_none            {false};
    bool m_VK_KHR_push_descriptor               {false};
    bool m_VK_KHR_portability_subset            {false};
};
class Capabilities
{
public:
    bool m_present_mode_fifo_latest_ready{false};
    bool m_surface_capabilities2         {false};
    bool m_surface_maintenance1          {false};
    bool m_swapchain_maintenance1        {false};
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
// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT and reset wholesale (with all
// allocated cbs freed) when the owning frame-in-flight slot's submit
// fence reports completion.
class Per_thread_command_pool
{
public:
    VkCommandPool                                command_pool{VK_NULL_HANDLE};
    std::vector<std::unique_ptr<Command_buffer>> allocated_command_buffers;
};

// Device-frame lifecycle state (OpenXR-style three phases, plus an
// optional nested swapchain-frame layer). Device_impl transitions through
// these as wait_frame/begin_frame/begin_swapchain_frame/... are called.
//
//   idle               --wait_frame()--------> waited
//   waited             --begin_frame(info)---> in_swapchain_frame (compat: combined)
//   waited             --begin_frame()-------> recording          (future: device-only)
//   recording          --begin_swapchain_frame--> in_swapchain_frame
//   in_swapchain_frame --end_swapchain_frame----> recording
//   recording          --end_frame()---------> idle               (future)
//   in_swapchain_frame --end_frame(info)-----> idle               (compat: combined)
enum class Device_frame_state : uint8_t
{
    idle,
    waited,
    recording,
    in_swapchain_frame
};

[[nodiscard]] auto c_str(Device_frame_state state) -> const char*;

class Device;
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

    [[nodiscard]] auto wait_frame () -> bool;
    [[nodiscard]] auto begin_frame() -> bool;
    [[nodiscard]] auto end_frame  () -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    void               wait_idle            ();
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    // Transitional: Command_buffer_impl took over wait_for_swapchain /
    // begin_swapchain / end_swapchain (was wait/begin/end_swapchain_frame
    // here). It still needs to flip m_had_swapchain_frame so the legacy
    // end_frame submit branch picks the swapchain path. Goes away with
    // the legacy single-cb-per-frame path.
    friend class Command_buffer_impl;

    void start_frame_capture();
    void end_frame_capture  ();

    // Active render pass tracking
    [[nodiscard]] static auto get_device_impl            () -> Device_impl*;
    // Allocate a fresh primary VkCommandBuffer from the pool owned by
    // (current_frame_in_flight_slot, thread_slot), wrap it in a new
    // Command_buffer that lives in the pool, and return that
    // Command_buffer by reference. The returned cb is in the initial
    // state -- the caller must call begin() on it before recording. Its
    // lifetime is tied to the pool: vkResetCommandPool runs when the
    // frame-in-flight slot's submit fence reports completion, freeing
    // every cb that was allocated from the pool that cycle.
    [[nodiscard]] auto get_command_buffer                (unsigned int thread_slot) -> Command_buffer&;

    [[nodiscard]] auto get_active_render_pass            () const -> VkRenderPass;
    [[nodiscard]] auto get_active_render_pass_impl       () const -> Render_pass_impl*;
                  void set_active_render_pass_impl       (Render_pass_impl* render_pass_impl);

    void submit_command_buffers   (std::span<Command_buffer* const> command_buffers);
    void add_completion_handler   (std::function<void(Device_impl&)> callback);
    void on_thread_enter          ();

    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (Command_buffer& init_command_buffer, const erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
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
    [[nodiscard]] auto get_allocator                      () -> VmaAllocator&;

    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const char* label);
    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const std::string& label);

    [[nodiscard]] auto get_sync_pool() -> Device_sync_pool&;

    [[nodiscard]] auto get_device                       () -> Device&;
    [[nodiscard]] auto get_surface                      () -> Surface*;
    [[nodiscard]] auto get_native_handles               () const -> Native_device_handles;
    [[nodiscard]] auto get_vulkan_instance              () -> VkInstance;
    [[nodiscard]] auto get_vulkan_physical_device       () -> VkPhysicalDevice;
    [[nodiscard]] auto get_vulkan_device                () -> VkDevice;
    [[nodiscard]] auto get_graphics_queue_family_index  () const -> uint32_t;
    [[nodiscard]] auto get_present_queue_family_index   () const -> uint32_t;
    [[nodiscard]] auto get_graphics_queue               () const -> VkQueue;
    [[nodiscard]] auto get_present_queue                () const -> VkQueue;
    [[nodiscard]] auto get_capabilities                 () const -> const Capabilities&;
    [[nodiscard]] auto get_driver_properties            () const -> const VkPhysicalDeviceDriverProperties&;
    [[nodiscard]] auto get_portability_subset_features  () const -> const VkPhysicalDevicePortabilitySubsetFeaturesKHR&;
    [[nodiscard]] auto get_portability_subset_properties() const -> const VkPhysicalDevicePortabilitySubsetPropertiesKHR&;
    [[nodiscard]] auto get_memory_type                  (uint32_t memory_type_index) const -> const VkMemoryType&;
    [[nodiscard]] auto get_memory_heap                  (uint32_t memory_heap_index) const -> const VkMemoryHeap&;
    [[nodiscard]] auto get_pipeline_cache               () const -> VkPipelineCache;
    [[nodiscard]] auto get_descriptor_set_layout        () const -> VkDescriptorSetLayout;
    [[nodiscard]] auto has_push_descriptor              () const -> bool;
    [[nodiscard]] auto get_texture_set_layout           () const -> VkDescriptorSetLayout;
    [[nodiscard]] auto get_cached_pipeline              (std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto create_graphics_pipeline         (const VkGraphicsPipelineCreateInfo& create_info, std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto get_or_create_graphics_pipeline  (const VkGraphicsPipelineCreateInfo& create_info, std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto get_or_create_compatible_render_pass(
        unsigned int                                   color_attachment_count,
        const std::array<erhe::dataformat::Format, 4>& color_attachment_formats,
        erhe::dataformat::Format                       depth_attachment_format,
        erhe::dataformat::Format                       stencil_attachment_format,
        unsigned int                                   sample_count,
        VkPipelineStageFlags                           incoming_src_stage  = 0,
        VkAccessFlags                                  incoming_src_access = 0,
        VkPipelineStageFlags                           incoming_dst_stage  = 0,
        VkAccessFlags                                  incoming_dst_access = 0,
        VkPipelineStageFlags                           outgoing_src_stage  = 0,
        VkAccessFlags                                  outgoing_src_access = 0,
        VkPipelineStageFlags                           outgoing_dst_stage  = 0,
        VkAccessFlags                                  outgoing_dst_access = 0
    ) -> VkRenderPass;
    [[nodiscard]] auto allocate_descriptor_set          () -> VkDescriptorSet;
    void               reset_descriptor_pool            ();

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
    [[nodiscard]] auto get_frame_index               () const -> uint64_t;
    [[nodiscard]] auto get_frame_in_flight_index     () const -> uint64_t;

    // Per-frame-in-flight submission resources used by Swapchain_impl. Device_impl
    // owns the command pools and their command buffers; the submit_fence slot is
    // filled/recycled by Swapchain_impl (it owns the fence pool).
    [[nodiscard]] auto get_device_frame_in_flight        (size_t index) -> Device_frame_in_flight&;
    void               ensure_device_frame_command_buffer(size_t index);
    void               reset_device_frame_command_pool   (size_t index);
    void               ensure_device_frame_slot          (size_t index);

    // Per-frame descriptor operation counters (for trace logging).
    // Public so Render_command_encoder_impl and Texture_heap_impl
    // can increment them without friend declarations.
    uint32_t m_desc_push_buf_count  {0};
    uint32_t m_desc_push_img_count  {0};
    uint32_t m_desc_alloc_set_count {0};
    uint32_t m_desc_heap_bind_count {0};
    uint32_t m_desc_draw_count      {0};

private:
    static constexpr size_t       s_number_of_frames_in_flight = 2;
    static constexpr unsigned int s_number_of_thread_slots     = 8;

    void update_frame_completion();

    // Single point of truth for transitioning m_state. Every assignment
    // to m_state goes through here so that one trace site captures all
    // transitions along with frame_index and slot -- critical for
    // diagnosing frame-lifecycle / slot-desync bugs. The site parameter
    // is a short literal naming the caller (e.g. "wait_frame",
    // "begin_frame", "end_frame"); it shows up in the trace and makes
    // the log skimmable.
    void set_state(Device_frame_state new_state, const char* site);

    [[nodiscard]] static auto get_physical_device_score(VkPhysicalDevice vulkan_physical_device, Surface_impl* surface_impl) -> float;
    [[nodiscard]] static auto query_device_queue_family_indices(
        VkPhysicalDevice vulkan_physical_device,
        Surface_impl*    surface_impl,
        uint32_t*        graphics_queue_family_index,
        uint32_t*        present_queue_family_index
    ) -> bool;
    static auto query_device_extensions(
        VkPhysicalDevice          vulkan_physical_device,
        Device_extensions&        device_extensions_out,
        std::vector<const char*>* device_extensions_c_str
    ) -> float;
    [[nodiscard]] auto choose_physical_device(Surface_impl* surface_impl, std::vector<const char*>& device_extensions_c_str) -> bool;

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
    std::vector<Completion_handler> m_completion_handlers;

    std::unique_ptr<Device_sync_pool>          m_sync_pool;

    VkInstance               m_vulkan_instance            {VK_NULL_HANDLE};
    VkPhysicalDevice         m_vulkan_physical_device     {VK_NULL_HANDLE};
    VkDevice                 m_vulkan_device              {VK_NULL_HANDLE};
    VmaAllocator             m_vma_allocator              {VK_NULL_HANDLE};
    VkDebugReportCallbackEXT m_debug_report_callback      {VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_utils_messenger      {VK_NULL_HANDLE};
    std::unique_ptr<Surface> m_surface                    {};
    VkQueue                  m_vulkan_graphics_queue      {VK_NULL_HANDLE};
    VkQueue                  m_vulkan_present_queue       {VK_NULL_HANDLE};
    uint32_t                 m_graphics_queue_family_index{0};
    uint32_t                 m_present_queue_family_index {0};

    // Per-frame-in-flight submission resources consumed by Swapchain_impl.
    // Device_impl owns the VkCommandPool + VkCommandBuffer; Swapchain_impl
    // populates/recycles submit_fence from its own fence pool.
    std::array<Device_frame_in_flight, s_number_of_frames_in_flight> m_device_submit_history{};

    // [frame_in_flight_slot][thread_slot] command pools + their
    // currently-allocated Command_buffers. Pools are created once at
    // Device_impl construction with VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    // and torn down at destruction. Each frame the per-slot pools are
    // wholesale-reset (vkResetCommandPool + clear the
    // allocated_command_buffers vector) once the slot's submit fence
    // reports GPU completion.
    std::array<
        std::array<Per_thread_command_pool, s_number_of_thread_slots>,
        s_number_of_frames_in_flight
    > m_command_pools{};

    VkSemaphore              m_vulkan_frame_end_semaphore {VK_NULL_HANDLE};
    uint64_t                 m_latest_completed_frame     {0}; // GPU
    uint64_t                 m_frame_index                {1}; // CPU

    // Current device-frame lifecycle state. Drives the assertions in
    // wait_frame / begin_frame / begin_swapchain_frame / end_swapchain_frame
    // / end_frame and backs is_in_device_frame / is_in_swapchain_frame.
    Device_frame_state       m_state{Device_frame_state::idle};
    // Set by Command_buffer_impl::begin_swapchain on success. Originally
    // read by the legacy end_frame() submit branch to pick
    // Swapchain_impl::end_frame; that branch is gone now (presentation
    // moved to submit_command_buffers). Kept as a debug breadcrumb and
    // cleared at end_frame() so the next frame starts fresh.
    bool                     m_had_swapchain_frame{false};

public:
    // Serializes recording into the active device frame command buffer
    // across worker threads (init taskflow etc). Only the in-frame paths
    // of upload_to_buffer / upload_to_texture / clear_texture /
    // transition_texture_layout and the Blit_command_encoder recording
    // scope need to hold this. At steady-state rendering the tick runs
    // single-threaded so the lock is uncontested.
    std::mutex m_recording_mutex;

private:


    Instance_layers          m_instance_layers    {};
    Instance_extensions      m_instance_extensions{};
    Device_extensions        m_device_extensions  {};
    Capabilities             m_capabilities       {};

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

    // Pipeline infrastructure
    VkPipelineCache                               m_pipeline_cache           {VK_NULL_HANDLE};
    VkDescriptorSetLayout                         m_descriptor_set_layout    {VK_NULL_HANDLE};
    VkDescriptorSetLayout                         m_texture_set_layout       {VK_NULL_HANDLE};
    VkDescriptorPool                              m_per_frame_descriptor_pool{VK_NULL_HANDLE};
    std::mutex                                    m_pipeline_map_mutex;
    std::unordered_map<std::size_t, VkPipeline>   m_pipeline_map;
    std::mutex                                    m_compatible_render_pass_mutex;
    std::unordered_map<std::size_t, VkRenderPass> m_compatible_render_pass_map;

    // For ring buffer:
    bool                                      m_need_sync{false};
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    std::size_t                               m_min_buffer_size = 2 * 1024 * 1024; // TODO

    // Active render pass tracking
    static Device_impl*  s_device_impl;
    Render_pass_impl*    m_active_render_pass{nullptr};

    // Optional hooks used when erhe::xr wraps Vulkan creation via
    // XR_KHR_vulkan_enable2. Stored as a raw pointer; lifetime is managed by
    // the caller (typically erhe::xr::Headset) and must outlive Device_impl
    // construction.
    const Vulkan_external_creators* m_external_creators{nullptr};
};

} // namespace erhe::graphics
