#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include "volk.h"
// vma forward declaration
VK_DEFINE_HANDLE(VmaAllocator)

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {

class Surface_impl;

class Device;
class Device_impl final
{
public:
    Device_impl   (Device& device, const Surface_create_info& surface_create_info);
    Device_impl   (const Device_impl&) = delete;
    void operator=(const Device_impl&) = delete;
    Device_impl   (Device_impl&&)      = delete;
    void operator=(Device_impl&&)      = delete;
    ~Device_impl  ();

    void start_of_frame        ();
    void end_of_frame          ();
    void memory_barrier        (Memory_barrier_mask barriers);
    void clear_texture         (Texture& texture, std::array<double, 4> clear_value);
    void upload_to_buffer      (Buffer& buffer, size_t offset, const void* data, size_t length);
    void add_completion_handler(std::function<void()> callback);
    void on_thread_enter       ();

    [[nodiscard]] auto get_handle                  (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture        () -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment        (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_number            () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry  (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder   () -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder() -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder (Render_pass& render_pass) -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties       (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto choose_depth_stencil_format (unsigned int flags, int sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor          () -> Shader_monitor&;
    [[nodiscard]] auto get_info                    () const -> const Device_info&;

    [[nodiscard]] auto create_render_pass(const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) -> VkResult;
    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const char* label);
    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const std::string& label);

    [[nodiscard]] auto get_surface                    () -> Surface*;
    [[nodiscard]] auto get_vulkan_instance            () -> VkInstance;
    [[nodiscard]] auto get_vulkan_device              () -> VkDevice;
    [[nodiscard]] auto get_graphics_queue_family_index() -> uint32_t const;
    [[nodiscard]] auto get_present_queue_family_index () -> uint32_t const;

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

private:
    void frame_completed(uint64_t frame);

    erhe::window::Context_window* m_context_window{nullptr};
    Device&                       m_device;
    Shader_monitor                m_shader_monitor;
    Device_info                   m_info;
    class Completion_handler
    {
    public:
        uint64_t              frame_number;
        std::function<void()> callback;
    };
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    uint64_t                                  m_frame_number{1};
    std::vector<Completion_handler>           m_completion_handlers;

    VkInstance               m_vulkan_instance       {VK_NULL_HANDLE};
    VkPhysicalDevice         m_vulkan_physical_device{VK_NULL_HANDLE};
    VkDevice                 m_vulkan_device         {VK_NULL_HANDLE};
    VmaAllocator             m_vma_allocator         {VK_NULL_HANDLE};
    VkDebugReportCallbackEXT m_debug_report_callback {VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_utils_messenger {VK_NULL_HANDLE};
    std::unique_ptr<Surface> m_surface               {};
    VkQueue                  m_vulkan_graphics_queue {VK_NULL_HANDLE};
    VkQueue                  m_vulkan_present_queue  {VK_NULL_HANDLE};
    uint32_t                 m_graphics_queue_family_index{0};
    uint32_t                 m_present_queue_family_index {0};

    bool m_VK_KHR_get_physical_device_properties2{false};
    bool m_VK_KHR_get_surface_capabilities2      {false};
    bool m_VK_KHR_surface                        {false};
    bool m_VK_KHR_surface_maintenance1           {false};
    bool m_VK_KHR_win32_surface                  {false};
    bool m_VK_EXT_debug_report                   {false};
    bool m_VK_EXT_debug_utils                    {false};
    bool m_VK_EXT_swapchain_colorspace           {false};
    bool m_VK_KHR_portability_enumeration        {false};

};

} // namespace erhe::graphics


// VK_EXT_descriptor_indexing
// VK_EXT_device_memory_report
// VK_EXT_extended_dynamic_state
// VK_EXT_extended_dynamic_state2
// VK_EXT_filter_cubic
// VK_EXT_image_view_min_lod
// VK_EXT_index_type_uint8
// VK_EXT_inline_uniform_block
// VK_EXT_load_store_op_none
// VK_EXT_multi_draw
// VK_EXT_pipeline_creation_cache_control
// VK_EXT_pipeline_creation_feedback
// VK_EXT_pipeline_protected_access
// VK_EXT_pipeline_robustness
// VK_EXT_sampler_filter_minmax
// VK_EXT_scalar_block_layout
// VK_EXT_separate_stencil_usage
// VK_EXT_shader_atomic_float
// VK_EXT_swapchain_maintenance1
// VK_EXT_tooling_info
// VK_GOOGLE_display_timing
// VK_KHR_16bit_storage
// VK_KHR_8bit_storage
// VK_KHR_bind_memory2
// VK_KHR_buffer_device_address
// VK_KHR_copy_commands2
// VK_KHR_create_renderpass2
// VK_KHR_dedicated_allocation
