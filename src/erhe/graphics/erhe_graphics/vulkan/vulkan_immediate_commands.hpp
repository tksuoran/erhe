#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/vulkan/vulkan_submit_handle.hpp"

#include "volk.h"

#include <array>
#include <vector>

namespace erhe::graphics {

class Vulkan_immediate_commands final
{
public:
    // the maximum number of command buffers which can similtaneously exist in the system; when we run out of buffers, we stall and wait until
    // an existing buffer becomes available
    static constexpr uint32_t kMaxCommandBuffers = 64;

    Vulkan_immediate_commands (Device& device, uint32_t queue_family_index, bool has_EXT_device_fault, const char* debug_name);
    ~Vulkan_immediate_commands();
    Vulkan_immediate_commands (const Vulkan_immediate_commands&) = delete;
    auto operator=(const Vulkan_immediate_commands&) -> Vulkan_immediate_commands& = delete;

    class Command_buffer_wrapper
    {
    public:
        VkCommandBuffer m_cmd_buf          {VK_NULL_HANDLE};
        VkCommandBuffer m_cmd_buf_allocated{VK_NULL_HANDLE};
        Submit_handle   m_handle           {};
        VkFence         m_fence            {VK_NULL_HANDLE};
        VkSemaphore     m_semaphore        {VK_NULL_HANDLE};
        bool            m_is_encoding      {false};
    };

    // returns the current command buffer (creates one if it does not exist)
    [[nodiscard]] auto acquire                      () -> const Command_buffer_wrapper&;
    [[nodiscard]] auto submit                       (const Command_buffer_wrapper& wrapper) -> Submit_handle;
                  void wait_semaphore               (VkSemaphore semaphore);
                  void signal_semaphore             (VkSemaphore semaphore, uint64_t signalValue);
    [[nodiscard]] auto acquire_last_submit_semaphore() -> VkSemaphore;
    [[nodiscard]] auto get_vk_fence                 (Submit_handle handle) const -> VkFence;
    [[nodiscard]] auto get_last_submit_handle       () const -> Submit_handle;
    [[nodiscard]] auto get_next_submit_handle       () const -> Submit_handle;
    [[nodiscard]] auto is_ready                     (Submit_handle handle, bool fastCheckNoVulkan = false) const -> bool;
                  void wait                         (Submit_handle handle);
                  void wait_all                     ();

private:
    void purge();

    VkDevice                                               m_device               {VK_NULL_HANDLE};
    VkQueue                                                m_queue                {VK_NULL_HANDLE};
    VkCommandPool                                          m_command_pool         {VK_NULL_HANDLE};
    uint32_t                                               m_queue_family_index   {0};
    bool                                                   m_has_EXT_device_fault {false};
    const char*                                            m_debug_name           {""};
    std::array<Command_buffer_wrapper, kMaxCommandBuffers> m_buffers;
    Submit_handle                                          m_last_submit_handle   {};
    Submit_handle                                          m_next_submit_handle   {};
    VkSemaphoreSubmitInfo                                  m_last_submit_semaphore{
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    };
    VkSemaphoreSubmitInfo                                  m_wait_semaphore{ // extra "wait" semaphore
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    };
    VkSemaphoreSubmitInfo                                  m_signal_semaphore{ // extra "signal" semaphore
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    };
    uint32_t                                               m_num_available_command_buffers{kMaxCommandBuffers};
    uint32_t                                               m_submit_counter{1};
};

} // namespace erhe::graphics
