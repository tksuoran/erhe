#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include "volk.h"

#include <sstream>

namespace erhe::graphics {

auto c_str(const VkResult result) -> const char*
{
    switch (result) {
        case VK_SUCCESS:                                            return "VK_SUCCESS";
        case VK_NOT_READY:                                          return "VK_NOT_READY";
        case VK_TIMEOUT:                                            return "VK_TIMEOUT";
        case VK_EVENT_SET:                                          return "VK_EVENT_SET";
        case VK_EVENT_RESET:                                        return "VK_EVENT_RESET";
        case VK_INCOMPLETE:                                         return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:                           return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:                         return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:                        return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:                                  return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:                            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:                            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:                        return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:                          return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:                          return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:                             return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:                         return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:                              return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:                                      return "VK_ERROR_UNKNOWN";
        case VK_ERROR_VALIDATION_FAILED:                            return "VK_ERROR_VALIDATION_FAILED";
        case VK_ERROR_OUT_OF_POOL_MEMORY:                           return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:                      return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:                                return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:               return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_PIPELINE_COMPILE_REQUIRED:                          return "VK_PIPELINE_COMPILE_REQUIRED";
        case VK_ERROR_NOT_PERMITTED:                                return "VK_ERROR_NOT_PERMITTED";
        case VK_ERROR_SURFACE_LOST_KHR:                             return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:                     return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:                                     return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:                              return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:                     return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_INVALID_SHADER_NV:                            return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR:                return "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR:       return "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR:    return "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR:       return "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR:        return "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR:          return "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:          return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_THREAD_IDLE_KHR:                                    return "VK_THREAD_IDLE_KHR";
        case VK_THREAD_DONE_KHR:                                    return "VK_THREAD_DONE_KHR";
        case VK_OPERATION_DEFERRED_KHR:                             return "VK_OPERATION_DEFERRED_KHR";
        case VK_OPERATION_NOT_DEFERRED_KHR:                         return "VK_OPERATION_NOT_DEFERRED_KHR";
        case VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR:             return "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR";
        case VK_ERROR_COMPRESSION_EXHAUSTED_EXT:                    return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
        case VK_INCOMPATIBLE_SHADER_BINARY_EXT:                     return "VK_INCOMPATIBLE_SHADER_BINARY_EXT";
        case VK_PIPELINE_BINARY_MISSING_KHR:                        return "VK_PIPELINE_BINARY_MISSING_KHR";
        case VK_ERROR_NOT_ENOUGH_SPACE_KHR:                         return "VK_ERROR_NOT_ENOUGH_SPACE_KHR";
        default: return "?";
    }
}
auto c_str(const VkDebugReportObjectTypeEXT object_type) -> const char*
{
    switch (object_type) {
        case VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT:                    return "UNKNOWN";
        case VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT:                   return "INSTANCE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT:            return "PHYSICAL_DEVICE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT:                     return "DEVICE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT:                      return "QUEUE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT:                  return "SEMAPHORE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT:             return "COMMAND_BUFFER";
        case VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT:                      return "FENCE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT:              return "DEVICE_MEMORY";
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT:                     return "BUFFER";
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT:                      return "IMAGE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT:                      return "EVENT";
        case VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT:                 return "QUERY_POOL";
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT:                return "BUFFER_VIEW";
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT:                 return "IMAGE_VIEW";
        case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT:              return "SHADER_MODULE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT:             return "PIPELINE_CACHE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT:            return "PIPELINE_LAYOUT";
        case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT:                return "RENDER_PASS";
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT:                   return "PIPELINE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT:      return "DESCRIPTOR_SET_LAYOUT";
        case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT:                    return "SAMPLER";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT:            return "DESCRIPTOR_POOL";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT:             return "DESCRIPTOR_SET";
        case VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT:                return "FRAMEBUFFER";
        case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT:               return "COMMAND_POOL";
        case VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT:                return "SURFACE_KHR";
        case VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT:              return "SWAPCHAIN_KHR";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT_EXT:  return "DEBUG_REPORT_CALLBACK_EXT";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_KHR_EXT:                return "DISPLAY_KHR";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_MODE_KHR_EXT:           return "DISPLAY_MODE_KHR";
        case VK_DEBUG_REPORT_OBJECT_TYPE_VALIDATION_CACHE_EXT_EXT:       return "VALIDATION_CACHE_EXT";
        case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_EXT:   return "SAMPLER_YCBCR_CONVERSION";
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_EXT: return "DESCRIPTOR_UPDATE_TEMPLATE";
        case VK_DEBUG_REPORT_OBJECT_TYPE_CU_MODULE_NVX_EXT:              return "CU_MODULE_NVX";
        case VK_DEBUG_REPORT_OBJECT_TYPE_CU_FUNCTION_NVX_EXT:            return "CU_FUNCTION_NVX";
        case VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR_EXT: return "ACCELERATION_STRUCTURE_KHR";
        case VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV_EXT:  return "ACCELERATION_STRUCTURE_NV";
        case VK_DEBUG_REPORT_OBJECT_TYPE_CUDA_MODULE_NV_EXT:             return "CUDA_MODULE_NV";
        case VK_DEBUG_REPORT_OBJECT_TYPE_CUDA_FUNCTION_NV_EXT:           return "CUDA_FUNCTION_NV";
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA_EXT:  return "BUFFER_COLLECTION_FUCHSIA";
        default: return "?";
    }
}
auto c_str(const VkObjectType object_type) -> const char*
{
    switch (object_type) {
        case VK_OBJECT_TYPE_UNKNOWN:                         return "UNKNOWN";
        case VK_OBJECT_TYPE_INSTANCE:                        return "INSTANCE";
        case VK_OBJECT_TYPE_PHYSICAL_DEVICE:                 return "PHYSICAL_DEVICE";
        case VK_OBJECT_TYPE_DEVICE:                          return "DEVICE";
        case VK_OBJECT_TYPE_QUEUE:                           return "QUEUE";
        case VK_OBJECT_TYPE_SEMAPHORE:                       return "SEMAPHORE";
        case VK_OBJECT_TYPE_COMMAND_BUFFER:                  return "COMMAND_BUFFER";
        case VK_OBJECT_TYPE_FENCE:                           return "FENCE";
        case VK_OBJECT_TYPE_DEVICE_MEMORY:                   return "DEVICE_MEMORY";
        case VK_OBJECT_TYPE_BUFFER:                          return "BUFFER";
        case VK_OBJECT_TYPE_IMAGE:                           return "IMAGE";
        case VK_OBJECT_TYPE_EVENT:                           return "EVENT";
        case VK_OBJECT_TYPE_QUERY_POOL:                      return "QUERY_POOL";
        case VK_OBJECT_TYPE_BUFFER_VIEW:                     return "BUFFER_VIEW";
        case VK_OBJECT_TYPE_IMAGE_VIEW:                      return "IMAGE_VIEW";
        case VK_OBJECT_TYPE_SHADER_MODULE:                   return "SHADER_MODULE";
        case VK_OBJECT_TYPE_PIPELINE_CACHE:                  return "PIPELINE_CACHE";
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT:                 return "PIPELINE_LAYOUT";
        case VK_OBJECT_TYPE_RENDER_PASS:                     return "RENDER_PASS";
        case VK_OBJECT_TYPE_PIPELINE:                        return "PIPELINE";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:           return "DESCRIPTOR_SET_LAYOUT";
        case VK_OBJECT_TYPE_SAMPLER:                         return "SAMPLER";
        case VK_OBJECT_TYPE_DESCRIPTOR_POOL:                 return "DESCRIPTOR_POOL";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET:                  return "DESCRIPTOR_SET";
        case VK_OBJECT_TYPE_FRAMEBUFFER:                     return "FRAMEBUFFER";
        case VK_OBJECT_TYPE_COMMAND_POOL:                    return "COMMAND_POOL";
        case VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION:        return "SAMPLER_YCBCR_CONVERSION";
        case VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE:      return "DESCRIPTOR_UPDATE_TEMPLATE";
        case VK_OBJECT_TYPE_PRIVATE_DATA_SLOT:               return "PRIVATE_DATA_SLOT";
        case VK_OBJECT_TYPE_SURFACE_KHR:                     return "SURFACE_KHR";
        case VK_OBJECT_TYPE_SWAPCHAIN_KHR:                   return "SWAPCHAIN_KHR";
        case VK_OBJECT_TYPE_DISPLAY_KHR:                     return "DISPLAY_KHR";
        case VK_OBJECT_TYPE_DISPLAY_MODE_KHR:                return "DISPLAY_MODE_KHR";
        case VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT:       return "DEBUG_REPORT_CALLBACK_EXT";
        case VK_OBJECT_TYPE_VIDEO_SESSION_KHR:               return "VIDEO_SESSION_KHR";
        case VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR:    return "VIDEO_SESSION_PARAMETERS_KHR";
        case VK_OBJECT_TYPE_CU_MODULE_NVX:                   return "CU_MODULE_NVX";
        case VK_OBJECT_TYPE_CU_FUNCTION_NVX:                 return "CU_FUNCTION_NVX";
        case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT:       return "DEBUG_UTILS_MESSENGER_EXT";
        case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR:      return "ACCELERATION_STRUCTURE_KHR";
        case VK_OBJECT_TYPE_VALIDATION_CACHE_EXT:            return "VALIDATION_CACHE_EXT";
        case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV:       return "ACCELERATION_STRUCTURE_NV";
        case VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL: return "PERFORMANCE_CONFIGURATION_INTEL";
        case VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR:          return "DEFERRED_OPERATION_KHR";
        case VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV:     return "INDIRECT_COMMANDS_LAYOUT_NV";
#ifdef VK_ENABLE_BETA_EXTENSIONS
        case VK_OBJECT_TYPE_CUDA_MODULE_NV:                  return "CUDA_MODULE_NV";
#endif
#ifdef VK_ENABLE_BETA_EXTENSIONS
        case VK_OBJECT_TYPE_CUDA_FUNCTION_NV:                return "CUDA_FUNCTION_NV";
#endif
        case VK_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA:       return "BUFFER_COLLECTION_FUCHSIA";
        case VK_OBJECT_TYPE_MICROMAP_EXT:                    return "MICROMAP_EXT";
        case VK_OBJECT_TYPE_TENSOR_ARM:                      return "TENSOR_ARM";
        case VK_OBJECT_TYPE_TENSOR_VIEW_ARM:                 return "TENSOR_VIEW_ARM";
        case VK_OBJECT_TYPE_OPTICAL_FLOW_SESSION_NV:         return "OPTICAL_FLOW_SESSION_NV";
        case VK_OBJECT_TYPE_SHADER_EXT:                      return "SHADER_EXT";
        case VK_OBJECT_TYPE_PIPELINE_BINARY_KHR:             return "PIPELINE_BINARY_KHR";
        case VK_OBJECT_TYPE_DATA_GRAPH_PIPELINE_SESSION_ARM: return "DATA_GRAPH_PIPELINE_SESSION_ARM";
        case VK_OBJECT_TYPE_EXTERNAL_COMPUTE_QUEUE_NV:       return "EXTERNAL_COMPUTE_QUEUE_NV";
        case VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT:    return "INDIRECT_COMMANDS_LAYOUT_EXT";
        case VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT:      return "INDIRECT_EXECUTION_SET_EXT";
        default: return "?";
    }
}
auto c_str(const VkPhysicalDeviceType device_type) -> const char*
{
    switch (device_type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:          return "VK_PHYSICAL_DEVICE_TYPE_OTHER";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "VK_PHYSICAL_DEVICE_TYPE_CPU";
        default: return "?";
    }
}
auto c_str(const VkDriverId driver_id) -> const char*
{
    switch (driver_id) {
        case VK_DRIVER_ID_AMD_PROPRIETARY:               return "VK_DRIVER_ID_AMD_PROPRIETARY";
        case VK_DRIVER_ID_AMD_OPEN_SOURCE:               return "VK_DRIVER_ID_AMD_OPEN_SOURCE";
        case VK_DRIVER_ID_MESA_RADV:                     return "VK_DRIVER_ID_MESA_RADV";
        case VK_DRIVER_ID_NVIDIA_PROPRIETARY:            return "VK_DRIVER_ID_NVIDIA_PROPRIETARY";
        case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:     return "VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS";
        case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:        return "VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA";
        case VK_DRIVER_ID_IMAGINATION_PROPRIETARY:       return "VK_DRIVER_ID_IMAGINATION_PROPRIETARY";
        case VK_DRIVER_ID_QUALCOMM_PROPRIETARY:          return "VK_DRIVER_ID_QUALCOMM_PROPRIETARY";
        case VK_DRIVER_ID_ARM_PROPRIETARY:               return "VK_DRIVER_ID_ARM_PROPRIETARY";
        case VK_DRIVER_ID_GOOGLE_SWIFTSHADER:            return "VK_DRIVER_ID_GOOGLE_SWIFTSHADER";
        case VK_DRIVER_ID_GGP_PROPRIETARY:               return "VK_DRIVER_ID_GGP_PROPRIETARY";
        case VK_DRIVER_ID_BROADCOM_PROPRIETARY:          return "VK_DRIVER_ID_BROADCOM_PROPRIETARY";
        case VK_DRIVER_ID_MESA_LLVMPIPE:                 return "VK_DRIVER_ID_MESA_LLVMPIPE";
        case VK_DRIVER_ID_MOLTENVK:                      return "VK_DRIVER_ID_MOLTENVK";
        case VK_DRIVER_ID_COREAVI_PROPRIETARY:           return "VK_DRIVER_ID_COREAVI_PROPRIETARY";
        case VK_DRIVER_ID_JUICE_PROPRIETARY:             return "VK_DRIVER_ID_JUICE_PROPRIETARY";
        case VK_DRIVER_ID_VERISILICON_PROPRIETARY:       return "VK_DRIVER_ID_VERISILICON_PROPRIETARY";
        case VK_DRIVER_ID_MESA_TURNIP:                   return "VK_DRIVER_ID_MESA_TURNIP";
        case VK_DRIVER_ID_MESA_V3DV:                     return "VK_DRIVER_ID_MESA_V3DV";
        case VK_DRIVER_ID_MESA_PANVK:                    return "VK_DRIVER_ID_MESA_PANVK";
        case VK_DRIVER_ID_SAMSUNG_PROPRIETARY:           return "VK_DRIVER_ID_SAMSUNG_PROPRIETARY";
        case VK_DRIVER_ID_MESA_VENUS:                    return "VK_DRIVER_ID_MESA_VENUS";
        case VK_DRIVER_ID_MESA_DOZEN:                    return "VK_DRIVER_ID_MESA_DOZEN";
        case VK_DRIVER_ID_MESA_NVK:                      return "VK_DRIVER_ID_MESA_NVK";
        case VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA:  return "VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA";
        case VK_DRIVER_ID_MESA_HONEYKRISP:               return "VK_DRIVER_ID_MESA_HONEYKRISP";
        case VK_DRIVER_ID_VULKAN_SC_EMULATION_ON_VULKAN: return "VK_DRIVER_ID_VULKAN_SC_EMULATION_ON_VULKAN";
        default: return "?";
    }
}

auto c_str(const VkFormat format) -> const char*
{
    switch (format) {
        case VK_FORMAT_UNDEFINED:                                     return "VK_FORMAT_UNDEFINED";
        case VK_FORMAT_R4G4_UNORM_PACK8:                              return "VK_FORMAT_R4G4_UNORM_PACK8";
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16:                         return "VK_FORMAT_R4G4B4A4_UNORM_PACK16";
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16:                         return "VK_FORMAT_B4G4R4A4_UNORM_PACK16";
        case VK_FORMAT_R5G6B5_UNORM_PACK16:                           return "VK_FORMAT_R5G6B5_UNORM_PACK16";
        case VK_FORMAT_B5G6R5_UNORM_PACK16:                           return "VK_FORMAT_B5G6R5_UNORM_PACK16";
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16:                         return "VK_FORMAT_R5G5B5A1_UNORM_PACK16";
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16:                         return "VK_FORMAT_B5G5R5A1_UNORM_PACK16";
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16:                         return "VK_FORMAT_A1R5G5B5_UNORM_PACK16";
        case VK_FORMAT_R8_UNORM:                                      return "VK_FORMAT_R8_UNORM";
        case VK_FORMAT_R8_SNORM:                                      return "VK_FORMAT_R8_SNORM";
        case VK_FORMAT_R8_USCALED:                                    return "VK_FORMAT_R8_USCALED";
        case VK_FORMAT_R8_SSCALED:                                    return "VK_FORMAT_R8_SSCALED";
        case VK_FORMAT_R8_UINT:                                       return "VK_FORMAT_R8_UINT";
        case VK_FORMAT_R8_SINT:                                       return "VK_FORMAT_R8_SINT";
        case VK_FORMAT_R8_SRGB:                                       return "VK_FORMAT_R8_SRGB";
        case VK_FORMAT_R8G8_UNORM:                                    return "VK_FORMAT_R8G8_UNORM";
        case VK_FORMAT_R8G8_SNORM:                                    return "VK_FORMAT_R8G8_SNORM";
        case VK_FORMAT_R8G8_USCALED:                                  return "VK_FORMAT_R8G8_USCALED";
        case VK_FORMAT_R8G8_SSCALED:                                  return "VK_FORMAT_R8G8_SSCALED";
        case VK_FORMAT_R8G8_UINT:                                     return "VK_FORMAT_R8G8_UINT";
        case VK_FORMAT_R8G8_SINT:                                     return "VK_FORMAT_R8G8_SINT";
        case VK_FORMAT_R8G8_SRGB:                                     return "VK_FORMAT_R8G8_SRGB";
        case VK_FORMAT_R8G8B8_UNORM:                                  return "VK_FORMAT_R8G8B8_UNORM";
        case VK_FORMAT_R8G8B8_SNORM:                                  return "VK_FORMAT_R8G8B8_SNORM";
        case VK_FORMAT_R8G8B8_USCALED:                                return "VK_FORMAT_R8G8B8_USCALED";
        case VK_FORMAT_R8G8B8_SSCALED:                                return "VK_FORMAT_R8G8B8_SSCALED";
        case VK_FORMAT_R8G8B8_UINT:                                   return "VK_FORMAT_R8G8B8_UINT";
        case VK_FORMAT_R8G8B8_SINT:                                   return "VK_FORMAT_R8G8B8_SINT";
        case VK_FORMAT_R8G8B8_SRGB:                                   return "VK_FORMAT_R8G8B8_SRGB";
        case VK_FORMAT_B8G8R8_UNORM:                                  return "VK_FORMAT_B8G8R8_UNORM";
        case VK_FORMAT_B8G8R8_SNORM:                                  return "VK_FORMAT_B8G8R8_SNORM";
        case VK_FORMAT_B8G8R8_USCALED:                                return "VK_FORMAT_B8G8R8_USCALED";
        case VK_FORMAT_B8G8R8_SSCALED:                                return "VK_FORMAT_B8G8R8_SSCALED";
        case VK_FORMAT_B8G8R8_UINT:                                   return "VK_FORMAT_B8G8R8_UINT";
        case VK_FORMAT_B8G8R8_SINT:                                   return "VK_FORMAT_B8G8R8_SINT";
        case VK_FORMAT_B8G8R8_SRGB:                                   return "VK_FORMAT_B8G8R8_SRGB";
        case VK_FORMAT_R8G8B8A8_UNORM:                                return "VK_FORMAT_R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SNORM:                                return "VK_FORMAT_R8G8B8A8_SNORM";
        case VK_FORMAT_R8G8B8A8_USCALED:                              return "VK_FORMAT_R8G8B8A8_USCALED";
        case VK_FORMAT_R8G8B8A8_SSCALED:                              return "VK_FORMAT_R8G8B8A8_SSCALED";
        case VK_FORMAT_R8G8B8A8_UINT:                                 return "VK_FORMAT_R8G8B8A8_UINT";
        case VK_FORMAT_R8G8B8A8_SINT:                                 return "VK_FORMAT_R8G8B8A8_SINT";
        case VK_FORMAT_R8G8B8A8_SRGB:                                 return "VK_FORMAT_R8G8B8A8_SRGB";
        case VK_FORMAT_B8G8R8A8_UNORM:                                return "VK_FORMAT_B8G8R8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SNORM:                                return "VK_FORMAT_B8G8R8A8_SNORM";
        case VK_FORMAT_B8G8R8A8_USCALED:                              return "VK_FORMAT_B8G8R8A8_USCALED";
        case VK_FORMAT_B8G8R8A8_SSCALED:                              return "VK_FORMAT_B8G8R8A8_SSCALED";
        case VK_FORMAT_B8G8R8A8_UINT:                                 return "VK_FORMAT_B8G8R8A8_UINT";
        case VK_FORMAT_B8G8R8A8_SINT:                                 return "VK_FORMAT_B8G8R8A8_SINT";
        case VK_FORMAT_B8G8R8A8_SRGB:                                 return "VK_FORMAT_B8G8R8A8_SRGB";
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:                         return "VK_FORMAT_A8B8G8R8_UNORM_PACK32";
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32:                         return "VK_FORMAT_A8B8G8R8_SNORM_PACK32";
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32:                       return "VK_FORMAT_A8B8G8R8_USCALED_PACK32";
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:                       return "VK_FORMAT_A8B8G8R8_SSCALED_PACK32";
        case VK_FORMAT_A8B8G8R8_UINT_PACK32:                          return "VK_FORMAT_A8B8G8R8_UINT_PACK32";
        case VK_FORMAT_A8B8G8R8_SINT_PACK32:                          return "VK_FORMAT_A8B8G8R8_SINT_PACK32";
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:                          return "VK_FORMAT_A8B8G8R8_SRGB_PACK32";
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:                      return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32:                      return "VK_FORMAT_A2R10G10B10_SNORM_PACK32";
        case VK_FORMAT_A2R10G10B10_USCALED_PACK32:                    return "VK_FORMAT_A2R10G10B10_USCALED_PACK32";
        case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:                    return "VK_FORMAT_A2R10G10B10_SSCALED_PACK32";
        case VK_FORMAT_A2R10G10B10_UINT_PACK32:                       return "VK_FORMAT_A2R10G10B10_UINT_PACK32";
        case VK_FORMAT_A2R10G10B10_SINT_PACK32:                       return "VK_FORMAT_A2R10G10B10_SINT_PACK32";
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:                      return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
        case VK_FORMAT_A2B10G10R10_SNORM_PACK32:                      return "VK_FORMAT_A2B10G10R10_SNORM_PACK32";
        case VK_FORMAT_A2B10G10R10_USCALED_PACK32:                    return "VK_FORMAT_A2B10G10R10_USCALED_PACK32";
        case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:                    return "VK_FORMAT_A2B10G10R10_SSCALED_PACK32";
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:                       return "VK_FORMAT_A2B10G10R10_UINT_PACK32";
        case VK_FORMAT_A2B10G10R10_SINT_PACK32:                       return "VK_FORMAT_A2B10G10R10_SINT_PACK32";
        case VK_FORMAT_R16_UNORM:                                     return "VK_FORMAT_R16_UNORM";
        case VK_FORMAT_R16_SNORM:                                     return "VK_FORMAT_R16_SNORM";
        case VK_FORMAT_R16_USCALED:                                   return "VK_FORMAT_R16_USCALED";
        case VK_FORMAT_R16_SSCALED:                                   return "VK_FORMAT_R16_SSCALED";
        case VK_FORMAT_R16_UINT:                                      return "VK_FORMAT_R16_UINT";
        case VK_FORMAT_R16_SINT:                                      return "VK_FORMAT_R16_SINT";
        case VK_FORMAT_R16_SFLOAT:                                    return "VK_FORMAT_R16_SFLOAT";
        case VK_FORMAT_R16G16_UNORM:                                  return "VK_FORMAT_R16G16_UNORM";
        case VK_FORMAT_R16G16_SNORM:                                  return "VK_FORMAT_R16G16_SNORM";
        case VK_FORMAT_R16G16_USCALED:                                return "VK_FORMAT_R16G16_USCALED";
        case VK_FORMAT_R16G16_SSCALED:                                return "VK_FORMAT_R16G16_SSCALED";
        case VK_FORMAT_R16G16_UINT:                                   return "VK_FORMAT_R16G16_UINT";
        case VK_FORMAT_R16G16_SINT:                                   return "VK_FORMAT_R16G16_SINT";
        case VK_FORMAT_R16G16_SFLOAT:                                 return "VK_FORMAT_R16G16_SFLOAT";
        case VK_FORMAT_R16G16B16_UNORM:                               return "VK_FORMAT_R16G16B16_UNORM";
        case VK_FORMAT_R16G16B16_SNORM:                               return "VK_FORMAT_R16G16B16_SNORM";
        case VK_FORMAT_R16G16B16_USCALED:                             return "VK_FORMAT_R16G16B16_USCALED";
        case VK_FORMAT_R16G16B16_SSCALED:                             return "VK_FORMAT_R16G16B16_SSCALED";
        case VK_FORMAT_R16G16B16_UINT:                                return "VK_FORMAT_R16G16B16_UINT";
        case VK_FORMAT_R16G16B16_SINT:                                return "VK_FORMAT_R16G16B16_SINT";
        case VK_FORMAT_R16G16B16_SFLOAT:                              return "VK_FORMAT_R16G16B16_SFLOAT";
        case VK_FORMAT_R16G16B16A16_UNORM:                            return "VK_FORMAT_R16G16B16A16_UNORM";
        case VK_FORMAT_R16G16B16A16_SNORM:                            return "VK_FORMAT_R16G16B16A16_SNORM";
        case VK_FORMAT_R16G16B16A16_USCALED:                          return "VK_FORMAT_R16G16B16A16_USCALED";
        case VK_FORMAT_R16G16B16A16_SSCALED:                          return "VK_FORMAT_R16G16B16A16_SSCALED";
        case VK_FORMAT_R16G16B16A16_UINT:                             return "VK_FORMAT_R16G16B16A16_UINT";
        case VK_FORMAT_R16G16B16A16_SINT:                             return "VK_FORMAT_R16G16B16A16_SINT";
        case VK_FORMAT_R16G16B16A16_SFLOAT:                           return "VK_FORMAT_R16G16B16A16_SFLOAT";
        case VK_FORMAT_R32_UINT:                                      return "VK_FORMAT_R32_UINT";
        case VK_FORMAT_R32_SINT:                                      return "VK_FORMAT_R32_SINT";
        case VK_FORMAT_R32_SFLOAT:                                    return "VK_FORMAT_R32_SFLOAT";
        case VK_FORMAT_R32G32_UINT:                                   return "VK_FORMAT_R32G32_UINT";
        case VK_FORMAT_R32G32_SINT:                                   return "VK_FORMAT_R32G32_SINT";
        case VK_FORMAT_R32G32_SFLOAT:                                 return "VK_FORMAT_R32G32_SFLOAT";
        case VK_FORMAT_R32G32B32_UINT:                                return "VK_FORMAT_R32G32B32_UINT";
        case VK_FORMAT_R32G32B32_SINT:                                return "VK_FORMAT_R32G32B32_SINT";
        case VK_FORMAT_R32G32B32_SFLOAT:                              return "VK_FORMAT_R32G32B32_SFLOAT";
        case VK_FORMAT_R32G32B32A32_UINT:                             return "VK_FORMAT_R32G32B32A32_UINT";
        case VK_FORMAT_R32G32B32A32_SINT:                             return "VK_FORMAT_R32G32B32A32_SINT";
        case VK_FORMAT_R32G32B32A32_SFLOAT:                           return "VK_FORMAT_R32G32B32A32_SFLOAT";
        case VK_FORMAT_R64_UINT:                                      return "VK_FORMAT_R64_UINT";
        case VK_FORMAT_R64_SINT:                                      return "VK_FORMAT_R64_SINT";
        case VK_FORMAT_R64_SFLOAT:                                    return "VK_FORMAT_R64_SFLOAT";
        case VK_FORMAT_R64G64_UINT:                                   return "VK_FORMAT_R64G64_UINT";
        case VK_FORMAT_R64G64_SINT:                                   return "VK_FORMAT_R64G64_SINT";
        case VK_FORMAT_R64G64_SFLOAT:                                 return "VK_FORMAT_R64G64_SFLOAT";
        case VK_FORMAT_R64G64B64_UINT:                                return "VK_FORMAT_R64G64B64_UINT";
        case VK_FORMAT_R64G64B64_SINT:                                return "VK_FORMAT_R64G64B64_SINT";
        case VK_FORMAT_R64G64B64_SFLOAT:                              return "VK_FORMAT_R64G64B64_SFLOAT";
        case VK_FORMAT_R64G64B64A64_UINT:                             return "VK_FORMAT_R64G64B64A64_UINT";
        case VK_FORMAT_R64G64B64A64_SINT:                             return "VK_FORMAT_R64G64B64A64_SINT";
        case VK_FORMAT_R64G64B64A64_SFLOAT:                           return "VK_FORMAT_R64G64B64A64_SFLOAT";
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:                       return "VK_FORMAT_B10G11R11_UFLOAT_PACK32";
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:                        return "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32";
        case VK_FORMAT_D16_UNORM:                                     return "VK_FORMAT_D16_UNORM";
        case VK_FORMAT_X8_D24_UNORM_PACK32:                           return "VK_FORMAT_X8_D24_UNORM_PACK32";
        case VK_FORMAT_D32_SFLOAT:                                    return "VK_FORMAT_D32_SFLOAT";
        case VK_FORMAT_S8_UINT:                                       return "VK_FORMAT_S8_UINT";
        case VK_FORMAT_D16_UNORM_S8_UINT:                             return "VK_FORMAT_D16_UNORM_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT:                             return "VK_FORMAT_D24_UNORM_S8_UINT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT:                            return "VK_FORMAT_D32_SFLOAT_S8_UINT";
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:                           return "VK_FORMAT_BC1_RGB_UNORM_BLOCK";
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:                            return "VK_FORMAT_BC1_RGB_SRGB_BLOCK";
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:                          return "VK_FORMAT_BC1_RGBA_UNORM_BLOCK";
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:                           return "VK_FORMAT_BC1_RGBA_SRGB_BLOCK";
        case VK_FORMAT_BC2_UNORM_BLOCK:                               return "VK_FORMAT_BC2_UNORM_BLOCK";
        case VK_FORMAT_BC2_SRGB_BLOCK:                                return "VK_FORMAT_BC2_SRGB_BLOCK";
        case VK_FORMAT_BC3_UNORM_BLOCK:                               return "VK_FORMAT_BC3_UNORM_BLOCK";
        case VK_FORMAT_BC3_SRGB_BLOCK:                                return "VK_FORMAT_BC3_SRGB_BLOCK";
        case VK_FORMAT_BC4_UNORM_BLOCK:                               return "VK_FORMAT_BC4_UNORM_BLOCK";
        case VK_FORMAT_BC4_SNORM_BLOCK:                               return "VK_FORMAT_BC4_SNORM_BLOCK";
        case VK_FORMAT_BC5_UNORM_BLOCK:                               return "VK_FORMAT_BC5_UNORM_BLOCK";
        case VK_FORMAT_BC5_SNORM_BLOCK:                               return "VK_FORMAT_BC5_SNORM_BLOCK";
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:                             return "VK_FORMAT_BC6H_UFLOAT_BLOCK";
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:                             return "VK_FORMAT_BC6H_SFLOAT_BLOCK";
        case VK_FORMAT_BC7_UNORM_BLOCK:                               return "VK_FORMAT_BC7_UNORM_BLOCK";
        case VK_FORMAT_BC7_SRGB_BLOCK:                                return "VK_FORMAT_BC7_SRGB_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:                       return "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:                        return "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:                     return "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:                      return "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:                     return "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:                      return "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK";
        case VK_FORMAT_EAC_R11_UNORM_BLOCK:                           return "VK_FORMAT_EAC_R11_UNORM_BLOCK";
        case VK_FORMAT_EAC_R11_SNORM_BLOCK:                           return "VK_FORMAT_EAC_R11_SNORM_BLOCK";
        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:                        return "VK_FORMAT_EAC_R11G11_UNORM_BLOCK";
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:                        return "VK_FORMAT_EAC_R11G11_SNORM_BLOCK";
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_4x4_UNORM_BLOCK";
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_4x4_SRGB_BLOCK";
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_5x4_UNORM_BLOCK";
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_5x4_SRGB_BLOCK";
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_5x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_5x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_6x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_6x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_6x6_UNORM_BLOCK";
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_6x6_SRGB_BLOCK";
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_8x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_8x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_8x6_UNORM_BLOCK";
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_8x6_SRGB_BLOCK";
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:                          return "VK_FORMAT_ASTC_8x8_UNORM_BLOCK";
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:                           return "VK_FORMAT_ASTC_8x8_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:                         return "VK_FORMAT_ASTC_10x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:                          return "VK_FORMAT_ASTC_10x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:                         return "VK_FORMAT_ASTC_10x6_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:                          return "VK_FORMAT_ASTC_10x6_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:                         return "VK_FORMAT_ASTC_10x8_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:                          return "VK_FORMAT_ASTC_10x8_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:                        return "VK_FORMAT_ASTC_10x10_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:                         return "VK_FORMAT_ASTC_10x10_SRGB_BLOCK";
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:                        return "VK_FORMAT_ASTC_12x10_UNORM_BLOCK";
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:                         return "VK_FORMAT_ASTC_12x10_SRGB_BLOCK";
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:                        return "VK_FORMAT_ASTC_12x12_UNORM_BLOCK";
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:                         return "VK_FORMAT_ASTC_12x12_SRGB_BLOCK";
        case VK_FORMAT_G8B8G8R8_422_UNORM:                            return "VK_FORMAT_G8B8G8R8_422_UNORM";
        case VK_FORMAT_B8G8R8G8_422_UNORM:                            return "VK_FORMAT_B8G8R8G8_422_UNORM";
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:                     return "VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM";
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:                      return "VK_FORMAT_G8_B8R8_2PLANE_420_UNORM";
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:                     return "VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM";
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:                      return "VK_FORMAT_G8_B8R8_2PLANE_422_UNORM";
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:                     return "VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM";
        case VK_FORMAT_R10X6_UNORM_PACK16:                            return "VK_FORMAT_R10X6_UNORM_PACK16";
        case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:                      return "VK_FORMAT_R10X6G10X6_UNORM_2PACK16";
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:            return "VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16";
        case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:        return "VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16";
        case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:        return "VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16";
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:    return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16";
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:     return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16";
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:    return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16";
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:     return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16";
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:    return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16";
        case VK_FORMAT_R12X4_UNORM_PACK16:                            return "VK_FORMAT_R12X4_UNORM_PACK16";
        case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:                      return "VK_FORMAT_R12X4G12X4_UNORM_2PACK16";
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:            return "VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16";
        case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:        return "VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16";
        case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:        return "VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16";
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:    return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16";
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:     return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16";
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:    return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16";
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:     return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16";
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:    return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16";
        case VK_FORMAT_G16B16G16R16_422_UNORM:                        return "VK_FORMAT_G16B16G16R16_422_UNORM";
        case VK_FORMAT_B16G16R16G16_422_UNORM:                        return "VK_FORMAT_B16G16R16G16_422_UNORM";
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:                  return "VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM";
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:                   return "VK_FORMAT_G16_B16R16_2PLANE_420_UNORM";
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:                  return "VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM";
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:                   return "VK_FORMAT_G16_B16R16_2PLANE_422_UNORM";
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:                  return "VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM";
        case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM:                      return "VK_FORMAT_G8_B8R8_2PLANE_444_UNORM";
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16:     return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16";
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16:     return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16";
        case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM:                   return "VK_FORMAT_G16_B16R16_2PLANE_444_UNORM";
        case VK_FORMAT_A4R4G4B4_UNORM_PACK16:                         return "VK_FORMAT_A4R4G4B4_UNORM_PACK16";
        case VK_FORMAT_A4B4G4R4_UNORM_PACK16:                         return "VK_FORMAT_A4B4G4R4_UNORM_PACK16";
        case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK:                         return "VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK:                        return "VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK:                        return "VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK:                        return "VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK:                       return "VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK:                       return "VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK";
        case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK:                       return "VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK";
        case VK_FORMAT_A1B5G5R5_UNORM_PACK16:                         return "VK_FORMAT_A1B5G5R5_UNORM_PACK16";
        case VK_FORMAT_A8_UNORM:                                      return "VK_FORMAT_A8_UNORM";
        case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:                   return "VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:                   return "VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:                   return "VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:                   return "VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:                    return "VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG";
        case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:                    return "VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:                    return "VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:                    return "VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG";
        case VK_FORMAT_R8_BOOL_ARM:                                   return "VK_FORMAT_R8_BOOL_ARM";
        case VK_FORMAT_R16G16_SFIXED5_NV:                             return "VK_FORMAT_R16G16_SFIXED5_NV";
        case VK_FORMAT_R10X6_UINT_PACK16_ARM:                         return "VK_FORMAT_R10X6_UINT_PACK16_ARM";
        case VK_FORMAT_R10X6G10X6_UINT_2PACK16_ARM:                   return "VK_FORMAT_R10X6G10X6_UINT_2PACK16_ARM";
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UINT_4PACK16_ARM:         return "VK_FORMAT_R10X6G10X6B10X6A10X6_UINT_4PACK16_ARM";
        case VK_FORMAT_R12X4_UINT_PACK16_ARM:                         return "VK_FORMAT_R12X4_UINT_PACK16_ARM";
        case VK_FORMAT_R12X4G12X4_UINT_2PACK16_ARM:                   return "VK_FORMAT_R12X4G12X4_UINT_2PACK16_ARM";
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UINT_4PACK16_ARM:         return "VK_FORMAT_R12X4G12X4B12X4A12X4_UINT_4PACK16_ARM";
        case VK_FORMAT_R14X2_UINT_PACK16_ARM:                         return "VK_FORMAT_R14X2_UINT_PACK16_ARM";
        case VK_FORMAT_R14X2G14X2_UINT_2PACK16_ARM:                   return "VK_FORMAT_R14X2G14X2_UINT_2PACK16_ARM";
        case VK_FORMAT_R14X2G14X2B14X2A14X2_UINT_4PACK16_ARM:         return "VK_FORMAT_R14X2G14X2B14X2A14X2_UINT_4PACK16_ARM";
        case VK_FORMAT_R14X2_UNORM_PACK16_ARM:                        return "VK_FORMAT_R14X2_UNORM_PACK16_ARM";
        case VK_FORMAT_R14X2G14X2_UNORM_2PACK16_ARM:                  return "VK_FORMAT_R14X2G14X2_UNORM_2PACK16_ARM";
        case VK_FORMAT_R14X2G14X2B14X2A14X2_UNORM_4PACK16_ARM:        return "VK_FORMAT_R14X2G14X2B14X2A14X2_UNORM_4PACK16_ARM";
        case VK_FORMAT_G14X2_B14X2R14X2_2PLANE_420_UNORM_3PACK16_ARM: return "VK_FORMAT_G14X2_B14X2R14X2_2PLANE_420_UNORM_3PACK16_ARM";
        case VK_FORMAT_G14X2_B14X2R14X2_2PLANE_422_UNORM_3PACK16_ARM: return "VK_FORMAT_G14X2_B14X2R14X2_2PLANE_422_UNORM_3PACK16_ARM";
        default: return "?";
    }
}

auto c_str(const VkColorSpaceKHR color_space) -> const char*
{
    switch (color_space) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:          return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:    return "VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT";
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:    return "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT";
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:       return "VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT";
        case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT:        return "VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT";
        case VK_COLOR_SPACE_BT709_LINEAR_EXT:            return "VK_COLOR_SPACE_BT709_LINEAR_EXT";
        case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:         return "VK_COLOR_SPACE_BT709_NONLINEAR_EXT";
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:           return "VK_COLOR_SPACE_BT2020_LINEAR_EXT";
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:            return "VK_COLOR_SPACE_HDR10_ST2084_EXT";
        case VK_COLOR_SPACE_DOLBYVISION_EXT:             return "VK_COLOR_SPACE_DOLBYVISION_EXT";
        case VK_COLOR_SPACE_HDR10_HLG_EXT:               return "VK_COLOR_SPACE_HDR10_HLG_EXT";
        case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT:         return "VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT";
        case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT:      return "VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT";
        case VK_COLOR_SPACE_PASS_THROUGH_EXT:            return "VK_COLOR_SPACE_PASS_THROUGH_EXT";
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return "VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT";
        case VK_COLOR_SPACE_DISPLAY_NATIVE_AMD:          return "VK_COLOR_SPACE_DISPLAY_NATIVE_AMD";
        default: return "?";
    }
}

auto c_str(const VkPresentModeKHR present_mode) -> const char*
{
    switch (present_mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:                 return "VK_PRESENT_MODE_IMMEDIATE_KHR";
        case VK_PRESENT_MODE_MAILBOX_KHR:                   return "VK_PRESENT_MODE_MAILBOX_KHR";
        case VK_PRESENT_MODE_FIFO_KHR:                      return "VK_PRESENT_MODE_FIFO_KHR";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:              return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
        case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:     return "VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR";
        case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return "VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR";
        case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR:         return "VK_PRESENT_MODE_FIFO_LATEST_READY_KHR";
        default: return "?";
    }
}

auto c_str(const VkDeviceFaultAddressTypeEXT type) -> const char*
{
    switch (type) {
        case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:                        return "VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:                return "VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:               return "VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:             return "VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT: return "VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT: return "VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:   return "VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT";
        default: return "?";
    }
}

auto to_string_VkDebugReportFlagsEXT(const VkDebugReportFlagsEXT flags) -> std::string
{
    std::stringstream ss;
    bool empty = true;
    if ((flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) != 0) {
        ss << "INFORMATION";
        empty = false;
    }
    if ((flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "WARNING";
        empty = false;
    }
    if ((flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "PERFORMANCE_WARNING";
        empty = false;
    }
    if ((flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "ERROR";
        empty = false;
    }
    if ((flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "DEBUG";
        empty = false;
    }
    if (empty) {
        ss << "0";
    }
    return ss.str();
}
auto to_string_VkDebugUtilsMessageSeverityFlagsEXT(const VkDebugUtilsMessageSeverityFlagsEXT severity) -> std::string
{
    std::stringstream ss;
    bool empty = true;
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0) {
        ss << "VERBOSE";
        empty = false;
    }
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "INFO";
        empty = false;
    }
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "WARNING";
        empty = false;
    }
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "ERROR";
        empty = false;
    }
    if (empty) {
        ss << "0";
    }
    return ss.str();
}
auto to_string_VkDebugUtilsMessageTypeFlagsEXT(const VkDebugUtilsMessageTypeFlagsEXT message_type) -> std::string
{
    std::stringstream ss;
    bool empty = true;
    if ((message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) != 0) {
        ss << "GENERAL";
        empty = false;
    }
    if ((message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "VALIDATION";
        empty = false;
    }
    if ((message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "PERFORMANCE";
        empty = false;
    }
    if ((message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "DEVICE_ADDRESS_BINDING";
        empty = false;
    }
    if (empty) {
        ss << "0";
    }
    return ss.str();
}
auto to_string_VkPresentScalingFlagsKHR(const VkPresentScalingFlagsKHR present_scaling) -> std::string
{
    std::stringstream ss;
    bool empty = true;
    if ((present_scaling & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR) != 0) {
        ss << "VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR";
        empty = false;
    }
    if ((present_scaling & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR";
        empty = false;
    }
    if ((present_scaling & VK_PRESENT_SCALING_STRETCH_BIT_KHR) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "VK_PRESENT_SCALING_STRETCH_BIT_KHR";
        empty = false;
    }
    if (empty) {
        ss << "0";
    }
    return ss.str();
}
auto to_string_VkPresentGravityFlagsKHR(const VkPresentGravityFlagsKHR present_gravity) -> std::string
{
    std::stringstream ss;
    bool empty = true;
    if ((present_gravity & VK_PRESENT_GRAVITY_MIN_BIT_KHR) != 0) {
        ss << "VK_PRESENT_GRAVITY_MIN_BIT_KHR";
        empty = false;
    }
    if ((present_gravity & VK_PRESENT_GRAVITY_MAX_BIT_KHR) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "VK_PRESENT_GRAVITY_MAX_BIT_KHR";
        empty = false;
    }
    if ((present_gravity & VK_PRESENT_GRAVITY_CENTERED_BIT_KHR) != 0) {
        if (!empty) {
            ss << "|";
        }
        ss << "VK_PRESENT_GRAVITY_CENTERED_BIT_KHR";
        empty = false;
    }
    if (empty) {
        ss << "0";
    }
    return ss.str();
}

auto to_vulkan(const erhe::dataformat::Format format) -> VkFormat
{
    switch (format) {
        case erhe::dataformat::Format::format_undefined               : return VK_FORMAT_UNDEFINED;

        case erhe::dataformat::Format::format_8_scalar_srgb           : return VK_FORMAT_R8_SRGB;
        case erhe::dataformat::Format::format_8_scalar_unorm          : return VK_FORMAT_R8_UNORM;
        case erhe::dataformat::Format::format_8_scalar_snorm          : return VK_FORMAT_R8_SNORM;
        case erhe::dataformat::Format::format_8_scalar_uscaled        : return VK_FORMAT_R8_USCALED;
        case erhe::dataformat::Format::format_8_scalar_sscaled        : return VK_FORMAT_R8_SSCALED;
        case erhe::dataformat::Format::format_8_scalar_uint           : return VK_FORMAT_R8_UINT;
        case erhe::dataformat::Format::format_8_scalar_sint           : return VK_FORMAT_R8_SINT;

        case erhe::dataformat::Format::format_8_vec2_srgb             : return VK_FORMAT_R8G8_SRGB;
        case erhe::dataformat::Format::format_8_vec2_unorm            : return VK_FORMAT_R8G8_UNORM;
        case erhe::dataformat::Format::format_8_vec2_snorm            : return VK_FORMAT_R8G8_SNORM;
        case erhe::dataformat::Format::format_8_vec2_uscaled          : return VK_FORMAT_R8G8_USCALED;
        case erhe::dataformat::Format::format_8_vec2_sscaled          : return VK_FORMAT_R8G8_SSCALED;
        case erhe::dataformat::Format::format_8_vec2_uint             : return VK_FORMAT_R8G8_UINT;
        case erhe::dataformat::Format::format_8_vec2_sint             : return VK_FORMAT_R8G8_SINT;

        case erhe::dataformat::Format::format_8_vec3_srgb             : return VK_FORMAT_R8G8B8_SRGB;
        case erhe::dataformat::Format::format_8_vec3_unorm            : return VK_FORMAT_R8G8B8_UNORM;
        case erhe::dataformat::Format::format_8_vec3_snorm            : return VK_FORMAT_R8G8B8_SNORM;
        case erhe::dataformat::Format::format_8_vec3_uscaled          : return VK_FORMAT_R8G8B8_USCALED;
        case erhe::dataformat::Format::format_8_vec3_sscaled          : return VK_FORMAT_R8G8B8_SSCALED;
        case erhe::dataformat::Format::format_8_vec3_uint             : return VK_FORMAT_R8G8B8_UINT;
        case erhe::dataformat::Format::format_8_vec3_sint             : return VK_FORMAT_R8G8B8_SINT;

        case erhe::dataformat::Format::format_8_vec4_srgb             : return VK_FORMAT_R8G8B8A8_SRGB;
        case erhe::dataformat::Format::format_8_vec4_unorm            : return VK_FORMAT_R8G8B8A8_UNORM;
        case erhe::dataformat::Format::format_8_vec4_snorm            : return VK_FORMAT_R8G8B8A8_SNORM;
        case erhe::dataformat::Format::format_8_vec4_uscaled          : return VK_FORMAT_R8G8B8A8_USCALED;
        case erhe::dataformat::Format::format_8_vec4_sscaled          : return VK_FORMAT_R8G8B8A8_SSCALED;
        case erhe::dataformat::Format::format_8_vec4_uint             : return VK_FORMAT_R8G8B8A8_UINT;
        case erhe::dataformat::Format::format_8_vec4_sint             : return VK_FORMAT_R8G8B8A8_SINT;

        case erhe::dataformat::Format::format_16_scalar_unorm         : return VK_FORMAT_R16_UNORM;
        case erhe::dataformat::Format::format_16_scalar_snorm         : return VK_FORMAT_R16_SNORM;
        case erhe::dataformat::Format::format_16_scalar_uscaled       : return VK_FORMAT_R16_USCALED;
        case erhe::dataformat::Format::format_16_scalar_sscaled       : return VK_FORMAT_R16_SSCALED;
        case erhe::dataformat::Format::format_16_scalar_uint          : return VK_FORMAT_R16_UINT;
        case erhe::dataformat::Format::format_16_scalar_sint          : return VK_FORMAT_R16_SINT;
        case erhe::dataformat::Format::format_16_scalar_float         : return VK_FORMAT_R16_SFLOAT;

        case erhe::dataformat::Format::format_16_vec2_unorm           : return VK_FORMAT_R16G16_UNORM;
        case erhe::dataformat::Format::format_16_vec2_snorm           : return VK_FORMAT_R16G16_SNORM;
        case erhe::dataformat::Format::format_16_vec2_uscaled         : return VK_FORMAT_R16G16_USCALED;
        case erhe::dataformat::Format::format_16_vec2_sscaled         : return VK_FORMAT_R16G16_SSCALED;
        case erhe::dataformat::Format::format_16_vec2_uint            : return VK_FORMAT_R16G16_UINT;
        case erhe::dataformat::Format::format_16_vec2_sint            : return VK_FORMAT_R16G16_SINT;
        case erhe::dataformat::Format::format_16_vec2_float           : return VK_FORMAT_R16G16_SFLOAT;

        case erhe::dataformat::Format::format_16_vec3_unorm           : return VK_FORMAT_R16G16B16_UNORM;
        case erhe::dataformat::Format::format_16_vec3_snorm           : return VK_FORMAT_R16G16B16_SNORM;
        case erhe::dataformat::Format::format_16_vec3_uscaled         : return VK_FORMAT_R16G16B16_USCALED;
        case erhe::dataformat::Format::format_16_vec3_sscaled         : return VK_FORMAT_R16G16B16_SSCALED;
        case erhe::dataformat::Format::format_16_vec3_uint            : return VK_FORMAT_R16G16B16_UINT;
        case erhe::dataformat::Format::format_16_vec3_sint            : return VK_FORMAT_R16G16B16_SINT;
        case erhe::dataformat::Format::format_16_vec3_float           : return VK_FORMAT_R16G16B16_SFLOAT;

        case erhe::dataformat::Format::format_16_vec4_unorm           : return VK_FORMAT_R16G16B16A16_UNORM;
        case erhe::dataformat::Format::format_16_vec4_snorm           : return VK_FORMAT_R16G16B16A16_SNORM;
        case erhe::dataformat::Format::format_16_vec4_uscaled         : return VK_FORMAT_R16G16B16A16_USCALED;
        case erhe::dataformat::Format::format_16_vec4_sscaled         : return VK_FORMAT_R16G16B16A16_SSCALED;
        case erhe::dataformat::Format::format_16_vec4_uint            : return VK_FORMAT_R16G16B16A16_UINT;
        case erhe::dataformat::Format::format_16_vec4_sint            : return VK_FORMAT_R16G16B16A16_SINT;
        case erhe::dataformat::Format::format_16_vec4_float           : return VK_FORMAT_R16G16B16A16_SFLOAT;

        case erhe::dataformat::Format::format_32_scalar_uint          : return VK_FORMAT_R32_UINT;
        case erhe::dataformat::Format::format_32_scalar_sint          : return VK_FORMAT_R32_SINT;
        case erhe::dataformat::Format::format_32_scalar_float         : return VK_FORMAT_R32_SFLOAT;

        case erhe::dataformat::Format::format_32_vec2_uint            : return VK_FORMAT_R32G32_UINT;
        case erhe::dataformat::Format::format_32_vec2_sint            : return VK_FORMAT_R32G32_SINT;
        case erhe::dataformat::Format::format_32_vec2_float           : return VK_FORMAT_R32G32_SFLOAT;

        case erhe::dataformat::Format::format_32_vec3_uint            : return VK_FORMAT_R32G32B32_UINT;
        case erhe::dataformat::Format::format_32_vec3_sint            : return VK_FORMAT_R32G32B32_SINT;
        case erhe::dataformat::Format::format_32_vec3_float           : return VK_FORMAT_R32G32B32_SFLOAT;

        case erhe::dataformat::Format::format_32_vec4_uint            : return VK_FORMAT_R32G32B32A32_UINT;
        case erhe::dataformat::Format::format_32_vec4_sint            : return VK_FORMAT_R32G32B32A32_SINT;
        case erhe::dataformat::Format::format_32_vec4_float           : return VK_FORMAT_R32G32B32A32_SFLOAT;

        case erhe::dataformat::Format::format_packed1010102_vec4_unorm: return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case erhe::dataformat::Format::format_packed1010102_vec4_snorm: return VK_FORMAT_A2R10G10B10_SNORM_PACK32;
        case erhe::dataformat::Format::format_packed1010102_vec4_uint : return VK_FORMAT_A2R10G10B10_UINT_PACK32;
        case erhe::dataformat::Format::format_packed1010102_vec4_sint : return VK_FORMAT_A2R10G10B10_SINT_PACK32;
        case erhe::dataformat::Format::format_packed111110_vec3_unorm : return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

        case erhe::dataformat::Format::format_d16_unorm               : return VK_FORMAT_D16_UNORM;           // a one-component, 16-bit unsigned normalized format that has a single 16-bit depth component.
        case erhe::dataformat::Format::format_x8_d24_unorm_pack32     : return VK_FORMAT_X8_D24_UNORM_PACK32; // a two-component, 32-bit format that has 24 unsigned normalized bits in the depth component and, optionally, 8 bits that are unused.
        case erhe::dataformat::Format::format_d32_sfloat              : return VK_FORMAT_D32_SFLOAT;          // a one-component, 32-bit signed floating-point format that has 32 bits in the depth component
        case erhe::dataformat::Format::format_s8_uint                 : return VK_FORMAT_S8_UINT;             // a one-component, 8-bit unsigned integer format that has 8 bits in the stencil component
        case erhe::dataformat::Format::format_d24_unorm_s8_uint       : return VK_FORMAT_D24_UNORM_S8_UINT;   // a two-component, 32-bit packed format that has 8 unsigned integer bits in the stencil component, and 24 unsigned normalized bits in the depth component
        case erhe::dataformat::Format::format_d32_sfloat_s8_uint      : return VK_FORMAT_D32_SFLOAT_S8_UINT;  // a two-component format that has 32 signed float bits in the depth component and 8 unsigned integer bits in the stencil component.

        default: return VK_FORMAT_UNDEFINED;
    }
}

auto to_erhe(const VkFormat format) -> erhe::dataformat::Format
{
    switch (format) {
        case VK_FORMAT_UNDEFINED               : return erhe::dataformat::Format::format_undefined               ;
        case VK_FORMAT_R8_SRGB                 : return erhe::dataformat::Format::format_8_scalar_srgb           ;
        case VK_FORMAT_R8_UNORM                : return erhe::dataformat::Format::format_8_scalar_unorm          ;
        case VK_FORMAT_R8_SNORM                : return erhe::dataformat::Format::format_8_scalar_snorm          ;
        case VK_FORMAT_R8_USCALED              : return erhe::dataformat::Format::format_8_scalar_uscaled        ;
        case VK_FORMAT_R8_SSCALED              : return erhe::dataformat::Format::format_8_scalar_sscaled        ;
        case VK_FORMAT_R8_UINT                 : return erhe::dataformat::Format::format_8_scalar_uint           ;
        case VK_FORMAT_R8_SINT                 : return erhe::dataformat::Format::format_8_scalar_sint           ;
        case VK_FORMAT_R8G8_SRGB               : return erhe::dataformat::Format::format_8_vec2_srgb             ;
        case VK_FORMAT_R8G8_UNORM              : return erhe::dataformat::Format::format_8_vec2_unorm            ;
        case VK_FORMAT_R8G8_SNORM              : return erhe::dataformat::Format::format_8_vec2_snorm            ;
        case VK_FORMAT_R8G8_USCALED            : return erhe::dataformat::Format::format_8_vec2_uscaled          ;
        case VK_FORMAT_R8G8_SSCALED            : return erhe::dataformat::Format::format_8_vec2_sscaled          ;
        case VK_FORMAT_R8G8_UINT               : return erhe::dataformat::Format::format_8_vec2_uint             ;
        case VK_FORMAT_R8G8_SINT               : return erhe::dataformat::Format::format_8_vec2_sint             ;
        case VK_FORMAT_R8G8B8_SRGB             : return erhe::dataformat::Format::format_8_vec3_srgb             ;
        case VK_FORMAT_R8G8B8_UNORM            : return erhe::dataformat::Format::format_8_vec3_unorm            ;
        case VK_FORMAT_R8G8B8_SNORM            : return erhe::dataformat::Format::format_8_vec3_snorm            ;
        case VK_FORMAT_R8G8B8_USCALED          : return erhe::dataformat::Format::format_8_vec3_uscaled          ;
        case VK_FORMAT_R8G8B8_SSCALED          : return erhe::dataformat::Format::format_8_vec3_sscaled          ;
        case VK_FORMAT_R8G8B8_UINT             : return erhe::dataformat::Format::format_8_vec3_uint             ;
        case VK_FORMAT_R8G8B8_SINT             : return erhe::dataformat::Format::format_8_vec3_sint             ;
        case VK_FORMAT_R8G8B8A8_SRGB           : return erhe::dataformat::Format::format_8_vec4_srgb             ;
        case VK_FORMAT_R8G8B8A8_UNORM          : return erhe::dataformat::Format::format_8_vec4_unorm            ;
        case VK_FORMAT_R8G8B8A8_SNORM          : return erhe::dataformat::Format::format_8_vec4_snorm            ;
        case VK_FORMAT_R8G8B8A8_USCALED        : return erhe::dataformat::Format::format_8_vec4_uscaled          ;
        case VK_FORMAT_R8G8B8A8_SSCALED        : return erhe::dataformat::Format::format_8_vec4_sscaled          ;
        case VK_FORMAT_R8G8B8A8_UINT           : return erhe::dataformat::Format::format_8_vec4_uint             ;
        case VK_FORMAT_R8G8B8A8_SINT           : return erhe::dataformat::Format::format_8_vec4_sint             ;
        case VK_FORMAT_R16_UNORM               : return erhe::dataformat::Format::format_16_scalar_unorm         ;
        case VK_FORMAT_R16_SNORM               : return erhe::dataformat::Format::format_16_scalar_snorm         ;
        case VK_FORMAT_R16_USCALED             : return erhe::dataformat::Format::format_16_scalar_uscaled       ;
        case VK_FORMAT_R16_SSCALED             : return erhe::dataformat::Format::format_16_scalar_sscaled       ;
        case VK_FORMAT_R16_UINT                : return erhe::dataformat::Format::format_16_scalar_uint          ;
        case VK_FORMAT_R16_SINT                : return erhe::dataformat::Format::format_16_scalar_sint          ;
        case VK_FORMAT_R16_SFLOAT              : return erhe::dataformat::Format::format_16_scalar_float         ;
        case VK_FORMAT_R16G16_UNORM            : return erhe::dataformat::Format::format_16_vec2_unorm           ;
        case VK_FORMAT_R16G16_SNORM            : return erhe::dataformat::Format::format_16_vec2_snorm           ;
        case VK_FORMAT_R16G16_USCALED          : return erhe::dataformat::Format::format_16_vec2_uscaled         ;
        case VK_FORMAT_R16G16_SSCALED          : return erhe::dataformat::Format::format_16_vec2_sscaled         ;
        case VK_FORMAT_R16G16_UINT             : return erhe::dataformat::Format::format_16_vec2_uint            ;
        case VK_FORMAT_R16G16_SINT             : return erhe::dataformat::Format::format_16_vec2_sint            ;
        case VK_FORMAT_R16G16_SFLOAT           : return erhe::dataformat::Format::format_16_vec2_float           ;
        case VK_FORMAT_R16G16B16_UNORM         : return erhe::dataformat::Format::format_16_vec3_unorm           ;
        case VK_FORMAT_R16G16B16_SNORM         : return erhe::dataformat::Format::format_16_vec3_snorm           ;
        case VK_FORMAT_R16G16B16_USCALED       : return erhe::dataformat::Format::format_16_vec3_uscaled         ;
        case VK_FORMAT_R16G16B16_SSCALED       : return erhe::dataformat::Format::format_16_vec3_sscaled         ;
        case VK_FORMAT_R16G16B16_UINT          : return erhe::dataformat::Format::format_16_vec3_uint            ;
        case VK_FORMAT_R16G16B16_SINT          : return erhe::dataformat::Format::format_16_vec3_sint            ;
        case VK_FORMAT_R16G16B16_SFLOAT        : return erhe::dataformat::Format::format_16_vec3_float           ;
        case VK_FORMAT_R16G16B16A16_UNORM      : return erhe::dataformat::Format::format_16_vec4_unorm           ;
        case VK_FORMAT_R16G16B16A16_SNORM      : return erhe::dataformat::Format::format_16_vec4_snorm           ;
        case VK_FORMAT_R16G16B16A16_USCALED    : return erhe::dataformat::Format::format_16_vec4_uscaled         ;
        case VK_FORMAT_R16G16B16A16_SSCALED    : return erhe::dataformat::Format::format_16_vec4_sscaled         ;
        case VK_FORMAT_R16G16B16A16_UINT       : return erhe::dataformat::Format::format_16_vec4_uint            ;
        case VK_FORMAT_R16G16B16A16_SINT       : return erhe::dataformat::Format::format_16_vec4_sint            ;
        case VK_FORMAT_R16G16B16A16_SFLOAT     : return erhe::dataformat::Format::format_16_vec4_float           ;
        case VK_FORMAT_R32_UINT                : return erhe::dataformat::Format::format_32_scalar_uint          ;
        case VK_FORMAT_R32_SINT                : return erhe::dataformat::Format::format_32_scalar_sint          ;
        case VK_FORMAT_R32_SFLOAT              : return erhe::dataformat::Format::format_32_scalar_float         ;
        case VK_FORMAT_R32G32_UINT             : return erhe::dataformat::Format::format_32_vec2_uint            ;
        case VK_FORMAT_R32G32_SINT             : return erhe::dataformat::Format::format_32_vec2_sint            ;
        case VK_FORMAT_R32G32_SFLOAT           : return erhe::dataformat::Format::format_32_vec2_float           ;
        case VK_FORMAT_R32G32B32_UINT          : return erhe::dataformat::Format::format_32_vec3_uint            ;
        case VK_FORMAT_R32G32B32_SINT          : return erhe::dataformat::Format::format_32_vec3_sint            ;
        case VK_FORMAT_R32G32B32_SFLOAT        : return erhe::dataformat::Format::format_32_vec3_float           ;
        case VK_FORMAT_R32G32B32A32_UINT       : return erhe::dataformat::Format::format_32_vec4_uint            ;
        case VK_FORMAT_R32G32B32A32_SINT       : return erhe::dataformat::Format::format_32_vec4_sint            ;
        case VK_FORMAT_R32G32B32A32_SFLOAT     : return erhe::dataformat::Format::format_32_vec4_float           ;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return erhe::dataformat::Format::format_packed1010102_vec4_unorm;
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32: return erhe::dataformat::Format::format_packed1010102_vec4_snorm;
        case VK_FORMAT_A2R10G10B10_UINT_PACK32 : return erhe::dataformat::Format::format_packed1010102_vec4_uint ;
        case VK_FORMAT_A2R10G10B10_SINT_PACK32 : return erhe::dataformat::Format::format_packed1010102_vec4_sint ;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32 : return erhe::dataformat::Format::format_packed111110_vec3_unorm ;
        case VK_FORMAT_D16_UNORM               : return erhe::dataformat::Format::format_d16_unorm               ;
        case VK_FORMAT_X8_D24_UNORM_PACK32     : return erhe::dataformat::Format::format_x8_d24_unorm_pack32     ;
        case VK_FORMAT_D32_SFLOAT              : return erhe::dataformat::Format::format_d32_sfloat              ;
        case VK_FORMAT_S8_UINT                 : return erhe::dataformat::Format::format_s8_uint                 ;
        case VK_FORMAT_D24_UNORM_S8_UINT       : return erhe::dataformat::Format::format_d24_unorm_s8_uint       ;
        case VK_FORMAT_D32_SFLOAT_S8_UINT      : return erhe::dataformat::Format::format_d32_sfloat_s8_uint      ;
        default: return erhe::dataformat::Format::format_undefined;
    }
}

auto get_vulkan_sample_count(const int msaa_sample_count) -> VkSampleCountFlagBits
{
    if (msaa_sample_count <= 1) {
        return VK_SAMPLE_COUNT_1_BIT;
    }
    else if (msaa_sample_count <= 2) {
        return VK_SAMPLE_COUNT_2_BIT;
    }
    else if (msaa_sample_count <= 4) {
        return VK_SAMPLE_COUNT_4_BIT;
    }
    else if (msaa_sample_count <= 8) {
        return VK_SAMPLE_COUNT_8_BIT;
    }
    else if (msaa_sample_count <= 16) {
        return VK_SAMPLE_COUNT_16_BIT;
    }
    else if (msaa_sample_count <= 32) {
        return VK_SAMPLE_COUNT_32_BIT;
    } else {
        return VK_SAMPLE_COUNT_64_BIT;
    }
}

auto get_vulkan_image_usage_flags(const uint64_t usage_mask) -> VkImageUsageFlags
{
    using namespace erhe::utility;

    VkImageUsageFlags flags = 0;
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::transfer_src)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::transfer_dst)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::sampled)) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::storage)) {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::color_attachment)) {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::depth_stencil_attachment)) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::transient_attachment)) {
        flags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::input_attachment)) {
        flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }
    if (test_bit_set(usage_mask, Image_usage_flag_bit_mask::host_transfer)) {
        flags |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
    }
    return flags;
}

auto get_vulkan_image_aspect_flags(const erhe::dataformat::Format format) -> VkImageAspectFlags
{
    const bool color   = erhe::dataformat::has_color            (format);
    const bool depth   = erhe::dataformat::get_depth_size_bits  (format) > 0;
    const bool stencil = erhe::dataformat::get_stencil_size_bits(format) > 0;

    VkImageAspectFlags vk_aspect_flags = 0;
    if (color) {
        vk_aspect_flags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }
    if (depth) {
        vk_aspect_flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (stencil) {
        vk_aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return vk_aspect_flags;
}

auto to_vulkan_buffer_usage(const Buffer_usage buffer_usage) -> VkBufferUsageFlags
{
    using namespace erhe::utility;

    VkBufferUsageFlags vk_flags{0};
    if (test_bit_set(buffer_usage, Buffer_usage::vertex)) {
        vk_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::index)) {
        vk_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::uniform)) {
        vk_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::storage)) {
        vk_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::indirect)) {
        vk_flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::uniform_texel)) {
        vk_flags |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::storage_texel)) {
        vk_flags |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::transfer_src)) {
        vk_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (test_bit_set(buffer_usage, Buffer_usage::transfer_dst)) {
        vk_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return vk_flags;
};

auto to_vulkan_memory_allocation_create_flags(const uint64_t flags) -> VmaAllocationCreateFlags
{
    using namespace erhe::utility;

    VmaAllocationCreateFlags vk_flags{0};
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::dedicated_allocation)) {
        vk_flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::never_allocate)) {
        vk_flags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::mapped)) {
        vk_flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::upper_address)) {
        vk_flags |= VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::dont_bind)) {
        vk_flags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::within_budget)) {
        vk_flags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::can_alias)) {
        vk_flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::strategy_min_memory)) {
        vk_flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::strategy_min_time)) {
        vk_flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
    }
    if (test_bit_set(flags, Memory_allocation_create_flag_bit_mask::strategy_min_offset)) {
        vk_flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT;
    }
    return vk_flags;
}

auto to_vulkan_memory_property_flags(const uint64_t flags) -> VkMemoryPropertyFlags
{
    using namespace erhe::utility;

    VkMemoryPropertyFlags vk_flags{0};
    if (test_bit_set(flags, Memory_property_flag_bit_mask::host_read)) {
        vk_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }
    if (test_bit_set(flags, Memory_property_flag_bit_mask::host_write)) {
        vk_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }
    if (test_bit_set(flags, Memory_property_flag_bit_mask::host_coherent)) {
        vk_flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    if (test_bit_set(flags, Memory_property_flag_bit_mask::host_cached)) {
        vk_flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    if (test_bit_set(flags, Memory_property_flag_bit_mask::device_local)) {
        vk_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    if (test_bit_set(flags, Memory_property_flag_bit_mask::lazily_allocated)) {
        vk_flags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }
    return vk_flags;
}

} // namespace erhe::graphics
