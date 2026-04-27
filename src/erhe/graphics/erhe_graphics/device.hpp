#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_utility/debug_label.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Format_properties
{
public:
    bool                 supported{false};

    // These are all texture_2d
    bool                 color_renderable{false};
    bool                 depth_renderable{false};
    bool                 stencil_renderable{false};
    bool                 filter{false};
    bool                 framebuffer_blend{false};

    int                  red_size        {0};
    int                  green_size      {0};
    int                  blue_size       {0};
    int                  alpha_size      {0};
    int                  depth_size      {0};
    int                  stencil_size    {0};
    int                  image_texel_size{0};

    std::vector<int>     texture_2d_sample_counts;
    int                  texture_2d_array_max_width{0};
    int                  texture_2d_array_max_height{0};
    int                  texture_2d_array_max_layers{0};

    // These are all texture_2d
    std::vector<int64_t> sparse_tile_x_sizes;
    std::vector<int64_t> sparse_tile_y_sizes;
    std::vector<int64_t> sparse_tile_z_sizes;
};

class Blit_command_encoder;
class Command_buffer;
class Command_encoder;
class Compute_command_encoder;
class Device;
class Render_command_encoder;
class Render_pass;
class Render_pipeline;
class Render_pipeline_create_info;
class Ring_buffer;
class Sampler;
class Surface;
class Swapchain;
class Texture;
class Vulkan_external_creators;

// Backend-neutral GPU and OS-platform handles needed by external integrations
// (notably OpenXR) to fill XrGraphicsBinding*KHR structs. Each backend
// populates only the fields relevant to it; the other fields stay zero.
// All handles are integer-typed or void* so this header pulls in no Vulkan,
// WGL or Wayland headers.
class Native_device_handles
{
public:
    // Vulkan backend
    uint64_t vk_instance           {0}; // VkInstance
    uint64_t vk_physical_device    {0}; // VkPhysicalDevice
    uint64_t vk_device             {0}; // VkDevice
    uint32_t vk_queue_family_index {0};
    uint32_t vk_queue_index        {0};

    // OpenGL Win32 backend
    void*    gl_hdc                {nullptr}; // HDC
    void*    gl_hglrc              {nullptr}; // HGLRC

    // OpenGL Wayland backend
    void*    gl_wl_display         {nullptr}; // struct wl_display*

    // OpenGL Xlib backend (currently unused; reserved for future Linux X11 path)
    void*    gl_xlib_display       {nullptr}; // Display*
    uint32_t gl_xlib_visualid      {0};
    void*    gl_glx_fb_config      {nullptr}; // GLXFBConfig
    void*    gl_glx_drawable       {nullptr}; // GLXDrawable
    void*    gl_glx_context        {nullptr}; // GLXContext
};

static constexpr unsigned int format_flag_require_depth     = 0x01u;
static constexpr unsigned int format_flag_require_stencil   = 0x02u;
static constexpr unsigned int format_flag_prefer_accuracy   = 0x04u;
static constexpr unsigned int format_flag_prefer_filterable = 0x08u;

// Image_layout is defined in enums.hpp

enum class Texture_heap_path : unsigned int {
    opengl_bindless_textures,   // GL_ARB_bindless_texture: sampler2D(uvec2_handle)
    opengl_sampler_array,       // OpenGL non-bindless: s_texture[handle.x] array
    metal_argument_buffer,      // Metal argument buffers
    vulkan_descriptor_indexing  // Vulkan: erhe_texture_heap[] at set 1 + assigned samplers at set 0
};

class Device_info
{
public:
    Vendor vendor{Vendor::Unknown};

    // Human-readable backend API identifier for diagnostics and window title.
    // Example: "OpenGL 4.1 Core", "Vulkan 1.3.0 VK_DRIVER_ID_MOLTENVK",
    // "Metal (Apple M1)", "Null".
    std::string api_info;

    int  glsl_version           {0};

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    int  gl_version             {0};
    bool core_profile           {false};
    bool compatibility_profile  {false};
    bool forward_compatible     {false};
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    uint32_t vulkan_api_version {0};
    uint32_t vulkan_driver_id   {0};
#endif

    bool use_clip_control            {false};
    erhe::math::Coordinate_conventions coordinate_conventions;
    bool use_direct_state_access     {false};
    bool use_binary_shaders          {false};
    bool use_integer_polygon_ids     {false};
    Texture_heap_path texture_heap_path{Texture_heap_path::opengl_sampler_array};

    // Helper queries
    [[nodiscard]] auto uses_bindless_texture       () const -> bool {
        return texture_heap_path == Texture_heap_path::opengl_bindless_textures;
    }

    bool use_sparse_texture          {false};
    bool use_persistent_buffers      {false};
    bool use_multi_draw_indirect_core{false};
    bool use_multi_draw_indirect_arb {false};
    bool emulate_multi_draw_indirect {false};
    bool use_compute_shader          {false};
    bool use_shader_storage_buffers  {false};
    bool use_base_instance           {false};
    bool use_clear_texture           {false};
    bool use_texture_view            {false};
    bool use_debug_output            {false}; // GL 4.3 or ARB_debug_output — debug callback
    bool use_debug_groups            {false}; // GL 4.3 — push/pop debug group (not in ARB_debug_output)

    // limits
    int max_compute_workgroup_count[3] = { 1, 1, 1 };
    int max_compute_workgroup_size [3] = { 1, 1, 1 };
    int max_compute_work_group_invocations       {1};
    int max_compute_shared_memory_size           {1};
    int max_samples                              {0};
    int max_color_texture_samples                {0};
    int max_depth_texture_samples                {0};
    int max_framebuffer_samples                  {0};
    int max_integer_samples                      {0};
    int max_vertex_attribs                       {0};

    int max_texture_size                        {64};
    int max_3d_texture_size                      {0};
    int max_cube_map_texture_size                {0};
    int max_texture_buffer_size                  {0};
    int max_array_texture_layers                 {0};
    int max_sparse_texture_size                  {0};

    uint32_t max_per_stage_descriptor_samplers   {0};
    int max_combined_texture_image_units         {0};  // combined across all shader stages
    int max_uniform_block_size                   {0};
    int max_shader_storage_buffer_bindings       {0};
    int max_uniform_buffer_bindings              {0};
    int max_compute_shader_storage_blocks        {0};
    int max_compute_uniform_blocks               {0};
    int max_vertex_shader_storage_blocks         {0};
    int max_vertex_uniform_blocks                {0};
    int max_vertex_uniform_vectors               {0};
    int max_fragment_shader_storage_blocks       {0};
    int max_fragment_uniform_blocks              {0};
    int max_fragment_uniform_vectors             {0};
    int max_geometry_shader_storage_blocks       {0};
    int max_geometry_uniform_blocks              {0};
    int max_tess_control_shader_storage_blocks   {0};
    int max_tess_control_uniform_blocks          {0};
    int max_tess_evaluation_shader_storage_blocks{0};
    int max_tess_evaluation_uniform_blocks       {0};
    float max_texture_max_anisotropy{1.0f};

    std::vector<int> msaa_sample_counts;
    int max_depth_layers    {4};
    int max_depth_resolution{64};

    // Depth/stencil MSAA resolve capabilities. Bitmasks built from
    // Resolve_mode_flag_bit_mask values. Vulkan: filled from
    // VkPhysicalDeviceDepthStencilResolveProperties. Metal: hardcoded to the
    // OS-version-fixed support set. GL: sample_zero only.
    uint32_t supported_depth_resolve_modes      {Resolve_mode_flag_bit_mask::sample_zero};
    uint32_t supported_stencil_resolve_modes    {Resolve_mode_flag_bit_mask::sample_zero};
    bool     independent_depth_stencil_resolve  {false}; // arbitrary depth/stencil mode pairs allowed
    bool     independent_depth_stencil_resolve_none{false}; // either-but-not-both NONE allowed

    // implementation defined
    unsigned int shader_storage_buffer_offset_alignment{256};
    unsigned int uniform_buffer_offset_alignment       {256};
};

using Shader_error_callback   = std::function<void(const std::string& error_log, const std::string& shader_source, const std::string& callstack)>;
using Device_message_callback = std::function<void(Message_severity severity, const std::string& message, const std::string& callstack)>;
using State_dump_callback     = std::function<void(const std::string& state_dump)>;
using Trace_callback          = std::function<void(const std::string& message)>;

class Frame_state;
class Frame_begin_info;
class Frame_end_info;

class Device_impl;
class Device final
{
public:
    Device(
        const Surface_create_info&      surface_create_info,
        const Graphics_config&          graphics_config,
        Device_message_callback         device_message_callback  = {},
        const Vulkan_external_creators* vulkan_external_creators = nullptr
    );
    Device         (const Device&) = delete;
    void operator= (const Device&) = delete;
    Device         (Device&&)      = delete;
    void operator= (Device&&)      = delete;
    ~Device() noexcept;

    // Device-frame lifecycle. A frame is bracketed by wait_frame() at the
    // top and end_frame() at the bottom, with one or more cb submits in
    // between. All cbs are obtained from get_command_buffer() and
    // committed via submit_command_buffers(). See notes.md
    // ("Frame lifecycle") for the full sequence.
    //
    //   wait_frame  -- pace the ring on the device's timeline semaphore,
    //                  recycle the per-(frame_in_flight, thread_slot)
    //                  command pool of the slot we are about to enter,
    //                  fire completion handlers.
    //   begin_frame -- (legacy single-cb shim) open the slot's legacy
    //                  command buffer for recording. New code should not
    //                  call this; record into the cb returned from
    //                  get_command_buffer() instead.
    //   end_frame   -- ADVANCE THE FRAME INDEX. THAT IS ALL.
    //                  Does NOT submit any command buffer.
    //                  Does NOT present any swapchain image.
    //                  Submission and presentation belong to
    //                  submit_command_buffers (which presents
    //                  implicitly when a cb engaged a swapchain via
    //                  Command_buffer::begin_swapchain). end_frame
    //                  signals the device timeline at the current
    //                  frame index, then increments it; the next
    //                  wait_frame() consumes that timeline value to
    //                  unblock pool reset.
    [[nodiscard]] auto wait_frame () -> bool;

    [[nodiscard]] auto begin_frame() -> bool;
    [[nodiscard]] auto end_frame  () -> bool;

    // Legacy compat overloads. Frame_begin_info / Frame_end_info are
    // ignored (presentation moved to submit_command_buffers). Kept only
    // so existing call sites compile while migrating.
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    // Blocks until all in-flight device frames complete; flushes pending
    // completion handlers. For init boundaries, not the steady-state loop.
    void wait_idle();

    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    void start_frame_capture       ();
    void end_frame_capture         ();

    // Allocate a fresh Command_buffer from the pool owned by
    // (current frame-in-flight slot, thread_slot). The returned cb is
    // in the initial state; the caller must call begin() before
    // recording. Lifetime is owned by the per-(frame_in_flight,
    // thread_slot) pool inside the backend Device_impl: the cb stays
    // valid until the next time the frame-in-flight slot completes a
    // GPU submit, at which point the pool is reset wholesale.
    [[nodiscard]] auto get_command_buffer(unsigned int thread_slot) -> Command_buffer&;

    // Submit a list of recorded Command_buffers to the graphics queue.
    // For each cb the backend collects the wait/signal semaphores and
    // CPU-side fence waits registered via Command_buffer::wait_for_*
    // / signal_*, splices them into a single VkSubmitInfo2 (Vulkan)
    // or equivalent, then submits.
    //
    // Implicit present: if any cb engaged a swapchain via
    // Command_buffer::begin_swapchain, the swapchain's per-slot
    // present semaphore is signalled by this submit and
    // vkQueuePresentKHR / swap_buffers / presentDrawable is driven
    // automatically right after the submit returns. Callers do not
    // need a separate present step.
    void submit_command_buffers    (std::span<Command_buffer* const> command_buffers);

    void add_completion_handler    (std::function<void()> callback);
    void on_thread_enter           ();

    [[nodiscard]] auto get_surface                        () -> Surface*;
    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    // Creates a tiny 2x2 fallback texture, populated with a debug
    // pattern. The pixel upload is recorded into init_command_buffer;
    // the caller must end + submit it (and wait on the GPU) before
    // the returned texture is sampled.
    [[nodiscard]] auto create_dummy_texture               (Command_buffer& init_command_buffer, erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_index                    () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder          (Command_buffer& command_buffer) -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder       (Command_buffer& command_buffer) -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder        (Command_buffer& command_buffer) -> Render_command_encoder;
    [[nodiscard]] auto create_render_pipeline             (const Render_pipeline_create_info& create_info) -> std::unique_ptr<Render_pipeline>;
    [[nodiscard]] auto get_format_properties              (erhe::dataformat::Format format) const -> Format_properties;

    // Probe whether a 2D VK_IMAGE_TILING_OPTIMAL image of the given format with
    // the supplied Image_usage_flag_bit_mask combination can actually be
    // created on this device. On Vulkan this calls
    // vkGetPhysicalDeviceImageFormatProperties2; on other backends it returns
    // true unconditionally (the probe concept is Vulkan-specific). Intended
    // for diagnosing XR swapchain usage-mask issues where the runtime may
    // allocate images with more usage bits than the app requests.
    [[nodiscard]] auto probe_image_format_support         (erhe::dataformat::Format format, uint64_t usage_mask) const -> bool;

    [[nodiscard]] auto get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>;
                  void sort_depth_stencil_formats         (std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const;
    [[nodiscard]] auto choose_depth_stencil_format        (const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format;
    [[nodiscard]] auto choose_depth_stencil_format        (unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor                 () -> Shader_monitor&;
    [[nodiscard]] auto get_info                           () const -> const Device_info&;
    [[nodiscard]] auto get_graphics_config                () const -> const Graphics_config&;
    [[nodiscard]] auto get_impl                           () -> Device_impl&;
    [[nodiscard]] auto get_impl                           () const -> const Device_impl&;
    [[nodiscard]] auto get_spirv_cache                    () -> Spirv_cache&;
    void               set_shader_error_callback          (Shader_error_callback callback);
    void               set_state_dump_callback            (State_dump_callback callback);
    void               set_trace_callback                 (Trace_callback callback);
    void               shader_error                       (const std::string& error_log, const std::string& shader_source);
    void               device_message                     (Message_severity severity, const std::string& message);
    void               state_dump                         (const std::string& dump);
    void               trace                              (const std::string& message);

    [[nodiscard]] auto get_active_render_pass             () const -> Render_pass*;
    void               set_active_render_pass             (Render_pass* render_pass);

    // Returns the underlying GPU and OS-platform handles needed to populate
    // XrGraphicsBinding*KHR structs. Backend-neutral: Vulkan backend fills
    // vk_*, OpenGL backend fills gl_* (HDC/HGLRC on Win32, wl_display on
    // Wayland), other backends return zero-initialized.
    [[nodiscard]] auto get_native_handles                 () const -> Native_device_handles;

private:
    Device_message_callback      m_device_message_callback{};
    std::unique_ptr<Device_impl> m_impl;
    Spirv_cache                  m_spirv_cache;
    Shader_error_callback        m_shader_error_callback  {};
    State_dump_callback          m_state_dump_callback    {};
    Trace_callback               m_trace_callback         {};
    Render_pass*                 m_active_render_pass     {nullptr};
};

[[nodiscard]] auto get_depth_clear_value_pointer(bool reverse_depth = true) -> const float *; // reverse_depth ? 0.0f : 1.0f;
[[nodiscard]] auto get_depth_function(Compare_operation depth_function, bool reverse_depth = true) -> Compare_operation;


} // namespace erhe::graphics
