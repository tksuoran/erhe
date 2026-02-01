#include "erhe_graphics/vulkan/vulkan_immediate_commands.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

Vulkan_immediate_commands::Vulkan_immediate_commands(
    Device&     device,
    uint32_t    queue_family_index,
    bool        has_EXT_device_fault,
    const char* debug_name
)
    : m_device              {device.get_impl().get_vulkan_device()}
    , m_queue_family_index  {queue_family_index}
    , m_has_EXT_device_fault{has_EXT_device_fault}
    , m_debug_name          {debug_name}
{
    vkGetDeviceQueue(m_device, queue_family_index, 0, &m_queue);

    const VkCommandPoolCreateInfo command_pool_create_info{
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext            = nullptr,
      .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = queue_family_index
    };

    VkResult result = VK_SUCCESS;
    result = vkCreateCommandPool(m_device, &command_pool_create_info, nullptr, &m_command_pool);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateCommandPool() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    //// ERHE_VERIFY(setDebugObjectName(device, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)m_command_pool, debug_name));

    const VkCommandBufferAllocateInfo command_bufgfer_allocate_info{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = m_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    for (uint32_t i = 0; i != kMaxCommandBuffers; i++) {
        Command_buffer_wrapper& buf = m_buffers[i];
        //char fenceName    [256] = {0};
        //char semaphoreName[256] = {0};
        //if (debug_name) {
        //    snprintf(fenceName,     sizeof(fenceName)     - 1, "Fence: %s (cmdbuf %u)", debugName, i);
        //    snprintf(semaphoreName, sizeof(semaphoreName) - 1, "Semaphore: %s (cmdbuf %u)", debugName, i);
        //}


        const VkSemaphoreCreateInfo semaphore_create_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .flags = 0,
        };
        result = vkCreateSemaphore(m_device, &semaphore_create_info, nullptr, &buf.m_semaphore);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        //VK_ASSERT(lvk::setDebugObjectName(device, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore, debugName));

        const VkFenceCreateInfo fence_create_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = 0, //isSignaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u,
        };
        result = vkCreateFence(m_device, &fence_create_info, nullptr, &buf.m_fence);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateFence() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        ///VK_ASSERT(lvk::setDebugObjectName(device, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, debugName));


        //buf.m_semaphore = createSemaphore(m_device, semaphoreName);
        //
        //buf.m_fence     = createFence    (m_device, fenceName);
        result = vkAllocateCommandBuffers(m_device, &command_bufgfer_allocate_info, &buf.m_cmd_buf_allocated);
        if (result != VK_SUCCESS) {
            log_context->critical("vkAllocateCommandBuffers() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
        m_buffers[i].m_handle.m_buffer_index = i;
    }
}

Vulkan_immediate_commands::~Vulkan_immediate_commands()
{
    wait_all();

    for (Command_buffer_wrapper& buf : m_buffers) {
        // lifetimes of all VkFence objects are managed explicitly we do not use deferredTask() for them
        vkDestroyFence    (m_device, buf.m_fence,     nullptr);
        vkDestroySemaphore(m_device, buf.m_semaphore, nullptr);
    }

    vkDestroyCommandPool(m_device, m_command_pool, nullptr);
}

void Vulkan_immediate_commands::purge()
{
    const uint32_t buffer_count = static_cast<uint32_t>(m_buffers.size());

    for (uint32_t i = 0; i != buffer_count; i++) {
        // always start checking with the oldest submitted buffer, then wrap around
        Command_buffer_wrapper& buf = m_buffers[(i + m_last_submit_handle.m_buffer_index + 1) % buffer_count];

        if (buf.m_cmd_buf == VK_NULL_HANDLE || buf.m_is_encoding) {
            continue;
        }

        VkResult wait_result = VK_SUCCESS;
        wait_result = vkWaitForFences(m_device, 1, &buf.m_fence, VK_TRUE, 0);

        if (wait_result == VK_SUCCESS) {
            VkResult result = VK_SUCCESS;
            result = vkResetCommandBuffer(buf.m_cmd_buf, VkCommandBufferResetFlags{0});
            if (result != VK_SUCCESS) {
                log_context->critical("vkResetCommandBuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
                abort();
            }

            result = vkResetFences(m_device, 1, &buf.m_fence);
            if (result != VK_SUCCESS) {
                log_context->critical("vkResetFences() failed with {} {}", static_cast<int32_t>(result), c_str(result));
                abort();
            }

            buf.m_cmd_buf = VK_NULL_HANDLE;
            m_num_available_command_buffers++;
        } else {
            if (wait_result != VK_TIMEOUT) {
                log_context->critical("vkWaitForFences() failed with {} {}", static_cast<int32_t>(wait_result), c_str(wait_result));
                abort();
            }
        }
    }
}

auto Vulkan_immediate_commands::acquire() -> const Vulkan_immediate_commands::Command_buffer_wrapper&
{
    while (m_num_available_command_buffers == 0) {
        log_context->info("Waiting for command buffers...");
        purge();
    }

    Vulkan_immediate_commands::Command_buffer_wrapper* current = nullptr;

    // we are ok with any available buffer
    for (Command_buffer_wrapper& buf : m_buffers) {
        if (buf.m_cmd_buf == VK_NULL_HANDLE) {
            current = &buf;
            break;
        }
    }
    if (current == nullptr) {
        log_context->critical("No available command buffers");
        abort();
    }
    ERHE_VERIFY(current->m_cmd_buf_allocated != VK_NULL_HANDLE);

    current->m_handle.m_submit_id = m_submit_counter;
    m_num_available_command_buffers--;

    current->m_cmd_buf = current->m_cmd_buf_allocated;
    current->m_is_encoding = true;
    const VkCommandBufferBeginInfo command_buffer_begin_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };

    VkResult result = vkBeginCommandBuffer(current->m_cmd_buf, &command_buffer_begin_info);
    if (result != VK_SUCCESS) {
        log_context->critical("vkBeginCommandBuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_next_submit_handle = current->m_handle;

    return *current;
}

void Vulkan_immediate_commands::wait(const Submit_handle handle)
{
    if (handle.empty()) {
        vkDeviceWaitIdle(m_device);
        return;
    }

    if (is_ready(handle)) {
        return;
    }

    if (!m_buffers[handle.m_buffer_index].m_is_encoding) {
        // we are waiting for a buffer which has not been submitted - this is probably a logic error somewhere in the calling code
        log_context->critical("waiting for a buffer which has not been submitted");
        abort();
    }

    VkResult result = vkWaitForFences(m_device, 1, &m_buffers[handle.m_buffer_index].m_fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        log_context->critical("vkWaitForFences() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    purge();
}

void Vulkan_immediate_commands::wait_all()
{
    VkFence fences[kMaxCommandBuffers];

    uint32_t num_fences = 0;
    for (const Command_buffer_wrapper& buf : m_buffers) {
        if (buf.m_cmd_buf != VK_NULL_HANDLE && !buf.m_is_encoding) {
            fences[num_fences++] = buf.m_fence;
        }
    }

    if (num_fences > 0) {
        VkResult result = VK_SUCCESS;
        result = vkWaitForFences(m_device, num_fences, fences, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_context->critical("vkWaitForFences() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
    }

    purge();
}

bool Vulkan_immediate_commands::is_ready(const Submit_handle handle, const bool fast_check_no_vulkan) const
{
    ERHE_VERIFY(handle.m_buffer_index < kMaxCommandBuffers);

    if (handle.empty()) {
        // a null handle
        return true;
    }

    const Command_buffer_wrapper& buf = m_buffers[handle.m_buffer_index];
    if (buf.m_cmd_buf == VK_NULL_HANDLE) {
        // already recycled and not yet reused
        return true;
    }

    if (buf.m_handle.m_submit_id != handle.m_submit_id) {
        // already recycled and reused by another command buffer
        return true;
    }

    if (fast_check_no_vulkan) {
        // do not ask the Vulkan API about it, just let it retire naturally (when m_submit_id for this m_buffer_index gets incremented)
        return false;
    }

    return vkWaitForFences(m_device, 1, &buf.m_fence, VK_TRUE, 0) == VK_SUCCESS;
}

auto Vulkan_immediate_commands::submit(const Command_buffer_wrapper& wrapper) -> Submit_handle
{
    ERHE_VERIFY(wrapper.m_is_encoding);

    VkResult result = VK_SUCCESS;
    result = vkEndCommandBuffer(wrapper.m_cmd_buf);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEndCommandBuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    VkSemaphoreSubmitInfo waitSemaphores[] = {{}, {}};
    uint32_t num_wait_semaphores = 0;
    if (m_wait_semaphore.semaphore) {
        waitSemaphores[num_wait_semaphores++] = m_wait_semaphore;
    }
    if (m_last_submit_semaphore.semaphore) {
        waitSemaphores[num_wait_semaphores++] = m_last_submit_semaphore;
    }
    VkSemaphoreSubmitInfo signal_semaphores[] = {
        VkSemaphoreSubmitInfo{
            .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext       = nullptr,
            .semaphore   = wrapper.m_semaphore,
            .value       = 0,
            .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .deviceIndex = 0
        },
        {},
    };
    uint32_t num_signal_semaphores = 1;
    if (m_signal_semaphore.semaphore) {
        signal_semaphores[num_signal_semaphores++] = m_signal_semaphore;
    }

    const VkCommandBufferSubmitInfo bufferSI{
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext         = nullptr,
        .commandBuffer = wrapper.m_cmd_buf,
        .deviceMask    = 0
    };
    const VkSubmitInfo2 si{
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext                    = nullptr,
        .flags                    = 0,
        .waitSemaphoreInfoCount   = num_wait_semaphores,
        .pWaitSemaphoreInfos      = waitSemaphores,
        .commandBufferInfoCount   = 1u,
        .pCommandBufferInfos      = &bufferSI,
        .signalSemaphoreInfoCount = num_signal_semaphores,
        .pSignalSemaphoreInfos    = signal_semaphores,
    };
    VkResult submit_result = vkQueueSubmit2(m_queue, 1u, &si, wrapper.m_fence);
    if (m_has_EXT_device_fault && (submit_result == VK_ERROR_DEVICE_LOST)) {
        VkDeviceFaultCountsEXT count{
            .sType            = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT,
            .pNext            = nullptr,
            .addressInfoCount = 0,
            .vendorInfoCount  = 0,
            .vendorBinarySize = 0
        };
        result = vkGetDeviceFaultInfoEXT(m_device, &count, nullptr);
        if (result != VK_SUCCESS) {
            log_context->critical("vkGetDeviceFaultInfoEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        std::vector<VkDeviceFaultAddressInfoEXT> addressInfo(count.addressInfoCount);
        std::vector<VkDeviceFaultVendorInfoEXT>  vendorInfo(count.vendorInfoCount);
        std::vector<uint8_t> binary(count.vendorBinarySize);
        VkDeviceFaultInfoEXT info = {
            .sType             = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
            .pAddressInfos     = addressInfo.data(),
            .pVendorInfos      = vendorInfo.data(),
            .pVendorBinaryData = binary.data(),
        };
        result = vkGetDeviceFaultInfoEXT(m_device, &count, &info);
        if (result != VK_SUCCESS) {
            log_context->critical("vkGetDeviceFaultInfoEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        log_context->error("VK_ERROR_DEVICE_LOST: %s\n", info.description);
        for (const VkDeviceFaultAddressInfoEXT& aInfo : addressInfo) {
            VkDeviceSize lowerAddress = aInfo.reportedAddress & ~(aInfo.addressPrecision - 1);
            VkDeviceSize upperAddress = aInfo.reportedAddress | (aInfo.addressPrecision - 1);
            log_context->info(
                "...address range [ {}, {} ]: {}",
                lowerAddress,
                upperAddress,
                c_str(aInfo.addressType)
            );
        }
        for (const VkDeviceFaultVendorInfoEXT& vInfo : vendorInfo) {
            log_context->info(
                "...caused by `{}` with error code {} and data {}",
                vInfo.description,
                vInfo.vendorFaultCode,
                vInfo.vendorFaultData
            );
        }
        const VkDeviceSize binarySize = count.vendorBinarySize;
        if (info.pVendorBinaryData && binarySize >= sizeof(VkDeviceFaultVendorBinaryHeaderVersionOneEXT)) {
            const VkDeviceFaultVendorBinaryHeaderVersionOneEXT* header = std::launder(
                reinterpret_cast<const VkDeviceFaultVendorBinaryHeaderVersionOneEXT*>(info.pVendorBinaryData)
            );
            const char hexDigits[] = "0123456789abcdef";
            char uuid[VK_UUID_SIZE * 2 + 1] = {};
            for (uint32_t i = 0; i < VK_UUID_SIZE; ++i) {
                uuid[i * 2 + 0] = hexDigits[(header->pipelineCacheUUID[i] >> 4) & 0xF];
                uuid[i * 2 + 1] = hexDigits[header->pipelineCacheUUID[i] & 0xF];
            }
            log_context->info("VkDeviceFaultVendorBinaryHeaderVersionOne:");
            log_context->info("   headerSize        : {}", header->headerSize);
            log_context->info("   headerVersion     : {}", (uint32_t)header->headerVersion);
            log_context->info("   vendorID          : {}", header->vendorID);
            log_context->info("   deviceID          : {}", header->deviceID);
            log_context->info("   driverVersion     : {}", header->driverVersion);
            log_context->info("   pipelineCacheUUID : {}", uuid);
            if (header->applicationNameOffset && header->applicationNameOffset < binarySize) {
                log_context->info("   applicationName   : {}", (const char*)info.pVendorBinaryData + header->applicationNameOffset);
            }
            log_context->info(
                "   applicationVersion: {}.{}.{}",
                VK_API_VERSION_MAJOR(header->applicationVersion),
                VK_API_VERSION_MINOR(header->applicationVersion),
                VK_API_VERSION_PATCH(header->applicationVersion));
            if (header->engineNameOffset && header->engineNameOffset < binarySize) {
                log_context->info("   engineName        : {}", (const char*)info.pVendorBinaryData + header->engineNameOffset);
            }
            log_context->info(
                "   engineVersion     : {}.{}.{}",
                VK_API_VERSION_MAJOR(header->engineVersion),
                VK_API_VERSION_MINOR(header->engineVersion),
                VK_API_VERSION_PATCH(header->engineVersion)
            );
            log_context->info(
                "   apiVersion        : {}.{}.{}.{}",
                VK_API_VERSION_MAJOR(header->apiVersion),
                VK_API_VERSION_MINOR(header->apiVersion),
                VK_API_VERSION_PATCH(header->apiVersion),
                VK_API_VERSION_VARIANT(header->apiVersion)
            );
        }
    }
    ERHE_VERIFY(result);

    m_last_submit_semaphore.semaphore = wrapper.m_semaphore;
    m_last_submit_handle = wrapper.m_handle;
    m_wait_semaphore.semaphore = VK_NULL_HANDLE;
    m_signal_semaphore.semaphore = VK_NULL_HANDLE;

    // reset
    const_cast<Command_buffer_wrapper&>(wrapper).m_is_encoding = false;
    m_submit_counter++;

    if (!m_submit_counter) {
        // skip the 0 value - when uint32_t wraps around (null Submit_handle)
        m_submit_counter++;
    }

    return m_last_submit_handle;
}

void Vulkan_immediate_commands::wait_semaphore(const VkSemaphore semaphore)
{
    ERHE_VERIFY(m_wait_semaphore.semaphore == VK_NULL_HANDLE);

    m_wait_semaphore.semaphore = semaphore;
}

void Vulkan_immediate_commands::signal_semaphore(const VkSemaphore semaphore, const uint64_t signal_value)
{
    ERHE_VERIFY(m_signal_semaphore.semaphore == VK_NULL_HANDLE);

    m_signal_semaphore.semaphore = semaphore;
    m_signal_semaphore.value = signal_value;
}

auto Vulkan_immediate_commands::acquire_last_submit_semaphore() -> VkSemaphore
{
    return std::exchange(m_last_submit_semaphore.semaphore, VK_NULL_HANDLE);
}

auto Vulkan_immediate_commands::get_vk_fence(const Submit_handle handle) const -> VkFence
{
    if (handle.empty()) {
        return VK_NULL_HANDLE;
    }

    return m_buffers[handle.m_buffer_index].m_fence;
}

auto Vulkan_immediate_commands::get_last_submit_handle() const -> Submit_handle
{
    return m_last_submit_handle;
}

auto Vulkan_immediate_commands::get_next_submit_handle() const -> Submit_handle
{
    return m_next_submit_handle;
}

} // namespace erhe::graphics
