#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#include <cstddef>
#include <string>
#include <vector>

namespace erhe::graphics {

[[nodiscard]] auto c_str(VkResult result) -> const char*;
[[nodiscard]] auto c_str(VkDebugReportObjectTypeEXT object_type) -> const char*;
[[nodiscard]] auto c_str(VkObjectType object_type) -> const char*;
[[nodiscard]] auto c_str(VkPhysicalDeviceType device_type) -> const char*;
[[nodiscard]] auto c_str(VkDriverId driver_id) -> const char*;
[[nodiscard]] auto c_str(VkFormat format) -> const char*;
[[nodiscard]] auto c_str(VkColorSpaceKHR color_space) -> const char*;
[[nodiscard]] auto c_str(VkPresentModeKHR present_mode) -> const char*;
[[nodiscard]] auto c_str(VkDeviceFaultAddressTypeKHR type) -> const char*;
[[nodiscard]] auto to_string_VkDeviceFaultFlagsKHR(VkDeviceFaultFlagsKHR flags) -> std::string;
[[nodiscard]] auto to_string_VkDebugReportFlagsEXT(const VkDebugReportFlagsEXT flags) -> std::string;
[[nodiscard]] auto to_string_VkDebugUtilsMessageSeverityFlagsEXT(const VkDebugUtilsMessageSeverityFlagsEXT severity) -> std::string;
[[nodiscard]] auto to_string_VkDebugUtilsMessageTypeFlagsEXT(const VkDebugUtilsMessageTypeFlagsEXT message_type) -> std::string;
[[nodiscard]] auto to_string_VkPresentScalingFlagsKHR(const VkPresentScalingFlagsKHR present_scaling) -> std::string;
[[nodiscard]] auto to_string_VkPresentGravityFlagsKHR(const VkPresentGravityFlagsKHR present_gravity) -> std::string;
[[nodiscard]] auto pipeline_stage_flags_str(VkPipelineStageFlags2 flags) -> std::string;
[[nodiscard]] auto access_flags_str        (VkAccessFlags2 flags) -> std::string;
[[nodiscard]] auto image_layout_str        (VkImageLayout layout) -> const char*;

[[nodiscard]] auto to_vulkan(erhe::dataformat::Format format) -> VkFormat;
[[nodiscard]] auto to_erhe  (VkFormat format) -> erhe::dataformat::Format;

[[nodiscard]] auto get_vulkan_sample_count      (int msaa_sample_count) -> VkSampleCountFlagBits;
[[nodiscard]] auto get_vulkan_image_usage_flags (uint64_t usage_mask) -> VkImageUsageFlags;
[[nodiscard]] auto get_vulkan_image_aspect_flags(erhe::dataformat::Format format) -> VkImageAspectFlags;

void cmd_pipeline_image_barriers2(
    VkCommandBuffer                cmd,
    uint32_t                       image_barrier_count,
    const VkImageMemoryBarrier2*   image_barriers
);

void log_image_layout_transition(
    const VkImageMemoryBarrier2& barrier,
    const char*                  source = nullptr
);

// Convert a read-back 4x8-bit color buffer (RGBA or BGRA order, per
// source_format) to tightly packed RGBA8 with forced opaque alpha (a
// composited swapchain frame's alpha is not meaningful). Used by the
// swapchain screenshot readback paths (emulated and WSI).
void convert_readback_to_rgba8_opaque(
    VkFormat                source_format,
    const std::byte*        source,
    std::size_t             pixel_count,
    std::vector<std::byte>& out_rgba8
);

[[nodiscard]] auto to_vk_primitive_topology (Primitive_type type) -> VkPrimitiveTopology;
[[nodiscard]] auto to_vk_polygon_mode       (Polygon_mode mode) -> VkPolygonMode;
[[nodiscard]] auto to_vk_cull_mode          (bool face_cull_enable, Cull_face_mode mode) -> VkCullModeFlags;
[[nodiscard]] auto to_vk_front_face         (Front_face_direction direction) -> VkFrontFace;
[[nodiscard]] auto to_vk_compare_op         (Compare_operation op) -> VkCompareOp;
[[nodiscard]] auto to_vk_stencil_op         (Stencil_op op) -> VkStencilOp;
[[nodiscard]] auto to_vk_stencil_op_state   (const Stencil_op_state& state) -> VkStencilOpState;
[[nodiscard]] auto to_vk_blend_factor       (Blending_factor factor) -> VkBlendFactor;
[[nodiscard]] auto to_vk_blend_op           (Blend_equation_mode mode) -> VkBlendOp;
[[nodiscard]] auto to_vk_vertex_format      (erhe::dataformat::Format format) -> VkFormat;
[[nodiscard]] auto to_vk_index_type         (erhe::dataformat::Format format) -> VkIndexType;

class Vk_stage_access
{
public:
    VkPipelineStageFlags stage;
    VkAccessFlags        access;
};
[[nodiscard]] auto usage_to_vk_layout      (uint64_t usage, bool is_depth_stencil, bool is_final_layout = false) -> VkImageLayout;
[[nodiscard]] auto usage_to_vk_stage_access(uint64_t usage, bool is_depth_stencil) -> Vk_stage_access;
[[nodiscard]] auto to_vk_image_layout      (Image_layout layout) -> VkImageLayout;

// Sync2 stage/access pair. Separate from Vk_stage_access (sync1) because the
// sync2 bit values diverge for buffer-relevant stages like VERTEX_ATTRIBUTE_INPUT
// that have no sync1 equivalent.
class Vk_stage_access_2
{
public:
    VkPipelineStageFlags2 stage;
    VkAccessFlags2        access;
};

// Maps a Buffer_usage mask to the precise set of consumer pipeline stages
// and access flags that can touch the buffer. Used to derive post-copy
// barriers so upload_to_buffer chains to exactly the stages the buffer was
// created for, not a conservative ALL_COMMANDS.
[[nodiscard]] auto buffer_usage_to_vk_stage_access(Buffer_usage usage) -> Vk_stage_access_2;

// Returns the canonical pair of subpass dependencies (EXTERNAL->0, 0->EXTERNAL)
// used by every render pass we create. The dependencies depend only on whether
// color and/or depth attachments are present, so the pipeline's compatibility
// render pass and the in-use render pass produce identical dependency arrays
// (the validation layer compares these as part of render-pass compatibility).
void make_canonical_subpass_dependencies(
    bool                has_color,
    bool                has_depth_stencil,
    VkSubpassDependency out_dependencies[2]
);

// Same as make_canonical_subpass_dependencies but for VkSubpassDependency2
// (renderpass2 path). Both helpers must produce dependencies that compare
// identically under render-pass compatibility checks.
void make_canonical_subpass_dependencies2(
    bool                 has_color,
    bool                 has_depth_stencil,
    VkSubpassDependency2 out_dependencies[2]
);

// Translate the cross-API Resolve_mode enum to the Vulkan flag bit. Returns
// VK_RESOLVE_MODE_NONE for any mode the spec doesn't recognize.
[[nodiscard]] auto to_vk_resolve_mode(Resolve_mode mode) -> VkResolveModeFlagBits;

[[nodiscard]] auto to_vulkan_buffer_usage(Buffer_usage buffer_usage) -> VkBufferUsageFlags;
[[nodiscard]] auto to_vulkan_memory_allocation_create_flags(uint64_t memory_allocation_create_flags) -> VmaAllocationCreateFlags;
[[nodiscard]] auto to_vulkan_memory_property_flags(uint64_t memory_property_flags) -> VkMemoryPropertyFlags;

} // namespace erhe::graphics
