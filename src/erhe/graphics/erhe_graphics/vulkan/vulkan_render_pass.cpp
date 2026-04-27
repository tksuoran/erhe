#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"

#include <fmt/format.h>

#include <string>
#include <thread>

namespace erhe::graphics {

namespace {

auto image_usage_mask_str(uint64_t mask) -> std::string
{
    if (mask == Image_usage_flag_bit_mask::invalid) {
        return "INVALID";
    }
    std::string out;
    auto add = [&](uint64_t bit, const char* name) {
        if ((mask & bit) != 0) {
            if (!out.empty()) { out += "|"; }
            out += name;
        }
    };
    add(Image_usage_flag_bit_mask::none,                     "none");
    add(Image_usage_flag_bit_mask::transfer_src,             "transfer_src");
    add(Image_usage_flag_bit_mask::transfer_dst,             "transfer_dst");
    add(Image_usage_flag_bit_mask::sampled,                  "sampled");
    add(Image_usage_flag_bit_mask::storage,                  "storage");
    add(Image_usage_flag_bit_mask::color_attachment,         "color_attachment");
    add(Image_usage_flag_bit_mask::depth_stencil_attachment, "depth_stencil_attachment");
    add(Image_usage_flag_bit_mask::transient_attachment,     "transient_attachment");
    add(Image_usage_flag_bit_mask::input_attachment,         "input_attachment");
    add(Image_usage_flag_bit_mask::host_transfer,            "host_transfer");
    add(Image_usage_flag_bit_mask::present,                  "present");
    add(Image_usage_flag_bit_mask::user_synchronized,        "user_synchronized");
    if (out.empty()) {
        out = fmt::format("0x{:x}", mask);
    }
    return out;
}

auto image_layout_str(Image_layout layout) -> const char*
{
    switch (layout) {
        case Image_layout::undefined:                        return "undefined";
        case Image_layout::general:                          return "general";
        case Image_layout::shader_read_only_optimal:         return "shader_read_only_optimal";
        case Image_layout::transfer_dst_optimal:             return "transfer_dst_optimal";
        case Image_layout::transfer_src_optimal:             return "transfer_src_optimal";
        case Image_layout::color_attachment_optimal:         return "color_attachment_optimal";
        case Image_layout::depth_stencil_attachment_optimal: return "depth_stencil_attachment_optimal";
        case Image_layout::depth_stencil_read_only_optimal:  return "depth_stencil_read_only_optimal";
        case Image_layout::present_src:                      return "present_src";
        default:                                             return "unknown";
    }
}

auto to_vk_load_op(Load_action action) -> VkAttachmentLoadOp
{
    switch (action) {
        case Load_action::Clear:     return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case Load_action::Load:      return VK_ATTACHMENT_LOAD_OP_LOAD;
        case Load_action::Dont_care: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default:                     return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

auto to_vk_store_op(Store_action action) -> VkAttachmentStoreOp
{
    switch (action) {
        case Store_action::Store:                          return VK_ATTACHMENT_STORE_OP_STORE;
        case Store_action::Dont_care:                      return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case Store_action::Multisample_resolve:            return VK_ATTACHMENT_STORE_OP_STORE;
        case Store_action::Store_and_multisample_resolve:  return VK_ATTACHMENT_STORE_OP_STORE;
        default:                                           return VK_ATTACHMENT_STORE_OP_STORE;
    }
}

auto vk_load_op_str(VkAttachmentLoadOp op) -> const char*
{
    switch (op) {
        case VK_ATTACHMENT_LOAD_OP_LOAD:      return "LOAD";
        case VK_ATTACHMENT_LOAD_OP_CLEAR:     return "CLEAR";
        case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return "DONT_CARE";
        case VK_ATTACHMENT_LOAD_OP_NONE:      return "NONE";
        default:                              return "?";
    }
}

auto vk_store_op_str(VkAttachmentStoreOp op) -> const char*
{
    switch (op) {
        case VK_ATTACHMENT_STORE_OP_STORE:     return "STORE";
        case VK_ATTACHMENT_STORE_OP_DONT_CARE: return "DONT_CARE";
        case VK_ATTACHMENT_STORE_OP_NONE:      return "NONE";
        default:                               return "?";
    }
}

auto vk_layout_str(VkImageLayout layout) -> const char*
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:                        return "UNDEFINED";
        case VK_IMAGE_LAYOUT_GENERAL:                          return "GENERAL";
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         return "COLOR_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:  return "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         return "SHADER_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             return "TRANSFER_SRC_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             return "TRANSFER_DST_OPTIMAL";
        case VK_IMAGE_LAYOUT_PREINITIALIZED:                   return "PREINITIALIZED";
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:         return "DEPTH_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:       return "STENCIL_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:          return "DEPTH_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:        return "STENCIL_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:                  return "PRESENT_SRC_KHR";
        default:                                                return "?";
    }
}

// Local aliases for the shared usage-to-Vulkan mapping functions
// Build an inter-pass memory barrier from the attachment descriptors
// of THIS render pass.
//
// Source side: derived from each attachment's usage_before, which
// describes how the resource was used prior to this pass. This is
// converted to Vulkan stage/access via usage_to_vk_stage_access().
// This replaces the previous approach of inferring src from the
// predecessor's attachment types (render_pass_before).
//
// Destination side: derived from which attachment types are present.
//
// FRAGMENT_SHADER + SHADER_READ is always included in the destination
// because any render pass may sample textures (e.g. shadow maps,
// prior-pass outputs) via descriptor sets. These descriptor-bound
// texture reads are not reflected in the attachment list, so we
// cannot conditionally omit them.
auto compute_inter_pass_barrier(
    const std::array<Render_pass_attachment_descriptor, 4>& color_attachments,
    const Render_pass_attachment_descriptor&                 depth_attachment,
    const Render_pass_attachment_descriptor&                 stencil_attachment
) -> VkMemoryBarrier2
{
    // --- Source side (prior usage of each attachment) ---
    VkPipelineStageFlags2 src_stage_mask  = 0;
    VkAccessFlags2        src_access_mask = 0;

    for (const Render_pass_attachment_descriptor& att : color_attachments) {
        if (att.is_defined() && (att.texture != nullptr) && (att.usage_before != Image_usage_flag_bit_mask::invalid)) {
            const Vk_stage_access sa = usage_to_vk_stage_access(att.usage_before, false);
            src_stage_mask  |= static_cast<VkPipelineStageFlags2>(sa.stage);
            src_access_mask |= static_cast<VkAccessFlags2>(sa.access);
        }
    }
    if (depth_attachment.is_defined() && (depth_attachment.texture != nullptr) && (depth_attachment.usage_before != Image_usage_flag_bit_mask::invalid)) {
        const Vk_stage_access sa = usage_to_vk_stage_access(depth_attachment.usage_before, true);
        src_stage_mask  |= static_cast<VkPipelineStageFlags2>(sa.stage);
        src_access_mask |= static_cast<VkAccessFlags2>(sa.access);
    }
    if (stencil_attachment.is_defined() && (stencil_attachment.texture != nullptr) && (stencil_attachment.usage_before != Image_usage_flag_bit_mask::invalid)) {
        const Vk_stage_access sa = usage_to_vk_stage_access(stencil_attachment.usage_before, true);
        src_stage_mask  |= static_cast<VkPipelineStageFlags2>(sa.stage);
        src_access_mask |= static_cast<VkAccessFlags2>(sa.access);
    }

    // Fallback if no attachment had a valid usage_before.
    if (src_stage_mask == 0) {
        src_stage_mask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        src_access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                        | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    // --- Destination side (what THIS pass consumes) ---
    // Always cover descriptor-bound texture sampling in fragment shaders.
    VkPipelineStageFlags2 dst_stage_mask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    VkAccessFlags2        dst_access_mask = VK_ACCESS_2_SHADER_READ_BIT;

    // Color attachments present -> pass writes/reads color
    for (const Render_pass_attachment_descriptor& att : color_attachments) {
        if (att.is_defined() && (att.texture != nullptr)) {
            dst_stage_mask  |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            dst_access_mask |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                            |  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        }
    }

    // Depth attachment present -> pass reads/writes depth
    if (depth_attachment.is_defined() && (depth_attachment.texture != nullptr)) {
        dst_stage_mask  |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        |  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        dst_access_mask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                        |  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    // Stencil attachment present (separate from depth)
    if (stencil_attachment.is_defined() && (stencil_attachment.texture != nullptr)) {
        dst_stage_mask  |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        |  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        dst_access_mask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                        |  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    return VkMemoryBarrier2{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask  = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
    };
}

// Build a post-renderpass memory barrier that makes attachment writes
// visible for the declared usage_after of each attachment (Option A).
//
// The VkRenderPass's subpass 0->EXTERNAL dependency makes attachment
// writes available but only extends the execution/memory dependency to
// COLOR_ATTACHMENT_OUTPUT (for color) or EARLY/LATE_FRAGMENT_TESTS (for
// depth). If usage_after includes stages outside that set -- for
// example FRAGMENT_SHADER when usage_after = sampled -- the writes are
// never made visible for those stages. This barrier closes that gap.
//
// Skipped when usage_after has the user_synchronized bit set; the
// caller is then responsible for inserting an explicit barrier.
//
// Returns true if a barrier was computed, false if none is needed.
auto compute_post_render_pass_barrier(
    const std::array<Render_pass_attachment_descriptor, 4>& color_attachments,
    const Render_pass_attachment_descriptor&                 depth_attachment,
    const Render_pass_attachment_descriptor&                 stencil_attachment,
    VkMemoryBarrier2&                                       out_barrier
) -> bool
{
    VkPipelineStageFlags2 src_stage_mask  = 0;
    VkAccessFlags2        src_access_mask = 0;
    VkPipelineStageFlags2 dst_stage_mask  = 0;
    VkAccessFlags2        dst_access_mask = 0;

    auto accumulate = [&](const Render_pass_attachment_descriptor& att, bool is_depth_stencil) {
        if (!att.is_defined() || (att.texture == nullptr)) {
            return;
        }
        if (att.usage_after == Image_usage_flag_bit_mask::invalid) {
            return;
        }
        // Skip if the caller takes responsibility for synchronization
        if (att.usage_after & Image_usage_flag_bit_mask::user_synchronized) {
            return;
        }
        // Strip the user_synchronized bit for stage/access lookup
        const uint64_t usage = att.usage_after & ~Image_usage_flag_bit_mask::user_synchronized;
        const Vk_stage_access dst_sa = usage_to_vk_stage_access(usage, is_depth_stencil);
        if (dst_sa.stage == 0) {
            return;
        }

        // Source: the write that the render pass performed on this attachment
        if (is_depth_stencil) {
            src_stage_mask  |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                            |  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            src_access_mask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        } else {
            src_stage_mask  |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            src_access_mask |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        dst_stage_mask  |= static_cast<VkPipelineStageFlags2>(dst_sa.stage);
        dst_access_mask |= static_cast<VkAccessFlags2>(dst_sa.access);
    };

    for (const Render_pass_attachment_descriptor& att : color_attachments) {
        accumulate(att, false);
    }
    accumulate(depth_attachment,   true);
    accumulate(stencil_attachment, true);

    if ((src_stage_mask == 0) || (dst_stage_mask == 0)) {
        return false;
    }

    out_barrier = VkMemoryBarrier2{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask  = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
    };
    return true;
}

} // anonymous namespace

auto Render_pass_impl::get_command_buffer() const -> VkCommandBuffer
{
    return m_command_buffer;
}

auto Render_pass_impl::get_render_pass() const -> VkRenderPass
{
    return m_render_pass;
}

Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& descriptor)
    : m_device              {device}
    , m_device_impl         {device.get_impl()}
    , m_swapchain           {descriptor.swapchain}
    , m_color_attachments   {descriptor.color_attachments}
    , m_depth_attachment    {descriptor.depth_attachment}
    , m_stencil_attachment  {descriptor.stencil_attachment}
    , m_render_target_width {descriptor.render_target_width}
    , m_render_target_height{descriptor.render_target_height}
    , m_debug_label         {descriptor.debug_label}
    , m_debug_group_name    {fmt::format("Render pass: {}", descriptor.debug_label.string_view())}
{
    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

    if (m_swapchain != nullptr) {
        // Swapchain render pass
        Surface_impl&                 surface_impl        = m_swapchain->get_impl().get_surface_impl();
        const Surface_create_info&    surface_create_info = surface_impl.get_surface_create_info();
        erhe::window::Context_window* context_window      = surface_create_info.context_window;
        ERHE_VERIFY(context_window != nullptr);

        const erhe::window::Window_configuration& window_configuration = context_window->get_window_configuration();

        Swapchain_impl& swapchain_impl = m_swapchain->get_impl();
        const VkSampleCountFlagBits sample_count = get_vulkan_sample_count(window_configuration.msaa_sample_count);
        const Render_pass_attachment_descriptor& color_att = descriptor.color_attachments[0];

        const VkAttachmentLoadOp swapchain_color_load_op = to_vk_load_op(color_att.load_action);
        if (swapchain_color_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            m_any_load_op_clear = true;
        }

        // The layout of the swapchain image at the start of a frame is not deterministic:
        // it is UNDEFINED on first use, and PRESENT_SRC_KHR after it has been presented at
        // least once. When the load op is CLEAR or DONT_CARE we advertise UNDEFINED so
        // the render pass drives a (discarding) transition itself, matching how the
        // swapchain depth attachment is handled below. This removes the need to pre-
        // transition every swapchain image from UNDEFINED to PRESENT_SRC_KHR at swapchain
        // init time. LOAD cannot discard, so it still honours the caller-supplied
        // layout_before.
        const VkImageLayout color_initial_layout =
            (swapchain_color_load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
                ? to_vk_image_layout(color_att.layout_before)
                : VK_IMAGE_LAYOUT_UNDEFINED;
        const VkImageLayout color_final_layout = to_vk_image_layout(color_att.layout_after);
        std::vector<VkAttachmentDescription2> attachment_descriptions;
        attachment_descriptions.push_back(
            VkAttachmentDescription2{
                .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .pNext          = nullptr,
                .flags          = 0,
                .format         = surface_impl.get_surface_format().format,
                .samples        = sample_count,
                .loadOp         = swapchain_color_load_op,
                .storeOp        = to_vk_store_op(color_att.store_action),
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = color_initial_layout,
                .finalLayout    = color_final_layout
            }
        );
        ERHE_VULKAN_SYNC_TRACE(
            "[RP_ATTACHMENT] pass=\"{}\" role=swapchain_color samples={} loadOp={} storeOp={} stencilLoadOp={} stencilStoreOp={} initialLayout={} finalLayout={}",
            m_debug_label.data(),
            static_cast<int>(sample_count),
            vk_load_op_str(swapchain_color_load_op),
            vk_store_op_str(to_vk_store_op(color_att.store_action)),
            vk_load_op_str(VK_ATTACHMENT_LOAD_OP_DONT_CARE),
            vk_store_op_str(VK_ATTACHMENT_STORE_OP_DONT_CARE),
            vk_layout_str(color_initial_layout),
            vk_layout_str(color_final_layout)
        );

        const VkAttachmentReference2 color_attachment_reference{
            .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .pNext      = nullptr,
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
        };

        m_clear_values.push_back(VkClearValue{
            .color = VkClearColorValue{
                .float32 = {
                    static_cast<float>(descriptor.color_attachments[0].clear_value[0]),
                    static_cast<float>(descriptor.color_attachments[0].clear_value[1]),
                    static_cast<float>(descriptor.color_attachments[0].clear_value[2]),
                    static_cast<float>(descriptor.color_attachments[0].clear_value[3])
                }
            }
        });

        // Depth attachment -- only if swapchain has a depth image
        const bool has_depth = swapchain_impl.get_depth_image_view() != VK_NULL_HANDLE;
        VkAttachmentReference2 depth_attachment_reference{
            .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .pNext      = nullptr,
            .attachment = VK_ATTACHMENT_UNUSED,
            .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT
        };
        if (has_depth) {
            const bool has_stencil = m_stencil_attachment.is_defined();
            const VkAttachmentLoadOp swapchain_depth_load_op   = to_vk_load_op(m_depth_attachment.load_action);
            const VkAttachmentLoadOp swapchain_stencil_load_op = has_stencil ? to_vk_load_op(m_stencil_attachment.load_action) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            if ((swapchain_depth_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) || (swapchain_stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)) {
                m_any_load_op_clear = true;
            }
            attachment_descriptions.push_back(
                VkAttachmentDescription2{
                    .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                    .pNext          = nullptr,
                    .flags          = 0,
                    .format         = swapchain_impl.get_vk_depth_format(),
                    .samples        = sample_count,
                    .loadOp         = swapchain_depth_load_op,
                    .storeOp        = to_vk_store_op(m_depth_attachment.store_action),
                    .stencilLoadOp  = swapchain_stencil_load_op,
                    .stencilStoreOp = has_stencil ? to_vk_store_op(m_stencil_attachment.store_action) : VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                }
            );
            ERHE_VULKAN_SYNC_TRACE(
                "[RP_ATTACHMENT] pass=\"{}\" role=swapchain_depth_stencil samples={} loadOp={} storeOp={} stencilLoadOp={} stencilStoreOp={} initialLayout={} finalLayout={}",
                m_debug_label.data(),
                static_cast<int>(sample_count),
                vk_load_op_str(swapchain_depth_load_op),
                vk_store_op_str(to_vk_store_op(m_depth_attachment.store_action)),
                vk_load_op_str(swapchain_stencil_load_op),
                vk_store_op_str(has_stencil ? to_vk_store_op(m_stencil_attachment.store_action) : VK_ATTACHMENT_STORE_OP_DONT_CARE),
                vk_layout_str(VK_IMAGE_LAYOUT_UNDEFINED),
                vk_layout_str(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            );
            depth_attachment_reference.attachment = 1;
            depth_attachment_reference.aspectMask =
                has_stencil
                    ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                    : VK_IMAGE_ASPECT_DEPTH_BIT;
            VkClearValue depth_clear{};
            depth_clear.depthStencil = VkClearDepthStencilValue{
                .depth   = static_cast<float>(m_depth_attachment.clear_value[0]),
                .stencil = has_stencil ? static_cast<uint32_t>(m_stencil_attachment.clear_value[0]) : 0u
            };
            m_clear_values.push_back(depth_clear);
        }

        const VkSubpassDescription2 subpass_description{
            .sType                   = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
            .pNext                   = nullptr,
            .flags                   = 0,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .viewMask                = 0,
            .inputAttachmentCount    = 0,
            .pInputAttachments       = nullptr,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &color_attachment_reference,
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = has_depth ? &depth_attachment_reference : nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments    = nullptr
        };

        // Use canonical dependencies that depend only on attachment structure
        // (has_color / has_depth) so the pipeline's compatibility render pass
        // and the in-use render pass produce identical dependency arrays.
        VkSubpassDependency2 canonical_dependencies[2];
        make_canonical_subpass_dependencies2(/*has_color=*/true, has_depth, canonical_dependencies);
        const VkRenderPassCreateInfo2 render_pass_create_info{
            .sType                   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
            .pNext                   = nullptr,
            .flags                   = 0,
            .attachmentCount         = static_cast<uint32_t>(attachment_descriptions.size()),
            .pAttachments            = attachment_descriptions.data(),
            .subpassCount            = 1,
            .pSubpasses              = &subpass_description,
            .dependencyCount         = 2,
            .pDependencies           = canonical_dependencies,
            .correlatedViewMaskCount = 0,
            .pCorrelatedViewMasks    = nullptr
        };

        result = vkCreateRenderPass2(vulkan_device, &render_pass_create_info, nullptr, &m_render_pass);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkCreateRenderPass2() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_RENDER_PASS,
            reinterpret_cast<uint64_t>(m_render_pass),
            "Render_pass_impl vulkan render_pass for surface/swapchain"
        );

    } else {
        // Non-swapchain (off-screen) render pass
        std::vector<VkAttachmentDescription2> attachment_descriptions;
        std::vector<VkAttachmentReference2>   color_attachment_references;
        std::vector<VkImageView>              image_views;
        bool                                  has_depth_stencil = false;
        uint32_t                              attachment_index  = 0;
        VkAttachmentReference2                depth_stencil_reference{
            .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .pNext      = nullptr,
            .attachment = VK_ATTACHMENT_UNUSED,
            .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT
        };

        // Color attachments (and resolve attachments)
        std::vector<VkAttachmentReference2> resolve_attachment_references;
        bool has_resolve = false;

        for (std::size_t i = 0; i < m_color_attachments.size(); ++i) {
            const Render_pass_attachment_descriptor& att = m_color_attachments[i];
            if (!att.is_defined()) {
                continue;
            }

            VkFormat vk_format = to_vulkan(att.texture->get_pixelformat());

            // If the texture has never been used yet, advertise UNDEFINED as the
            // initial layout so the render pass can drive the (discarding) transition
            // from UNDEFINED itself, instead of us emitting a wasteful UNDEFINED ->
            // read-only barrier before begin. This is only valid when loadOp is
            // CLEAR or DONT_CARE -- a LOAD with UNDEFINED initial layout is
            // invalid per Vulkan spec (VUID-VkAttachmentDescription2-format-06699).
            const bool fresh_color = (att.texture->get_impl().get_current_layout() == VK_IMAGE_LAYOUT_UNDEFINED);
            const bool wants_load  = (att.load_action == Load_action::Load);
            const VkImageLayout initial_layout = (fresh_color && !wants_load)
                ? VK_IMAGE_LAYOUT_UNDEFINED
                : to_vk_image_layout(att.layout_before);
            const VkImageLayout final_layout   = (att.resolve_texture != nullptr)
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                : to_vk_image_layout(att.layout_after);

            // Multisampled color attachments must not use LOAD/STORE: loadOp must
            // be CLEAR or DONT_CARE and storeOp must be DONT_CARE so the
            // implementation can keep the MSAA samples in tile / lazy memory.
            // Resolved data is exposed via the resolve attachment.
            const VkSampleCountFlagBits samples  = get_vulkan_sample_count(att.texture->get_sample_count());
            const bool                  msaa     = (samples != VK_SAMPLE_COUNT_1_BIT);
            VkAttachmentLoadOp          load_op  = to_vk_load_op(att.load_action);
            VkAttachmentStoreOp         store_op = to_vk_store_op(att.store_action);
            if (msaa) {
                // MSAA color: only drop Load/Store when the caller didn't
                // explicitly ask to preserve the MSAA samples. Plain Store
                // is a valid request -- needed for multi-render-pass
                // splits that want to Load the MSAA color in a follow-up
                // pass. Previously this unconditionally forced DONT_CARE
                // and silently discarded the attachment, which broke any
                // such split.
                if ((load_op == VK_ATTACHMENT_LOAD_OP_LOAD) && (att.load_action != Load_action::Load)) {
                    load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                }
                if ((att.store_action == Store_action::Store) ||
                    (att.store_action == Store_action::Store_and_multisample_resolve)) {
                    store_op = VK_ATTACHMENT_STORE_OP_STORE;
                } else {
                    store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }
            }
            if (load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                m_any_load_op_clear = true;
            }
            attachment_descriptions.push_back(VkAttachmentDescription2{
                .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .pNext          = nullptr,
                .flags          = 0,
                .format         = vk_format,
                .samples        = samples,
                .loadOp         = load_op,
                .storeOp        = store_op,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = initial_layout,
                .finalLayout    = final_layout
            });
            ERHE_VULKAN_SYNC_TRACE(
                "[RP_ATTACHMENT] pass=\"{}\" role=color{} tex=\"{}\" samples={} loadOp={} storeOp={} stencilLoadOp={} stencilStoreOp={} initialLayout={} finalLayout={}",
                m_debug_label.data(), i,
                att.texture->get_debug_label().data(),
                static_cast<int>(samples),
                vk_load_op_str(load_op),
                vk_store_op_str(store_op),
                vk_load_op_str(VK_ATTACHMENT_LOAD_OP_DONT_CARE),
                vk_store_op_str(VK_ATTACHMENT_STORE_OP_DONT_CARE),
                vk_layout_str(initial_layout),
                vk_layout_str(final_layout)
            );

            color_attachment_references.push_back(VkAttachmentReference2{
                .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                .pNext      = nullptr,
                .attachment = attachment_index,
                .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
            });

            VkImageView view = const_cast<Texture_impl&>(att.texture->get_impl()).get_vk_image_view(
                VK_IMAGE_ASPECT_COLOR_BIT,
                static_cast<uint32_t>(att.texture_layer),
                1,
                static_cast<uint32_t>(att.texture_level),
                1
            );
            image_views.push_back(view);

            VkClearValue clear_value{};
            clear_value.color = VkClearColorValue{
                .float32 = {
                    static_cast<float>(att.clear_value[0]),
                    static_cast<float>(att.clear_value[1]),
                    static_cast<float>(att.clear_value[2]),
                    static_cast<float>(att.clear_value[3])
                }
            };
            m_clear_values.push_back(clear_value);

            ++attachment_index;

            // Resolve attachment
            if (att.resolve_texture != nullptr) {
                has_resolve = true;
                VkFormat resolve_format = to_vulkan(att.resolve_texture->get_pixelformat());
                attachment_descriptions.push_back(VkAttachmentDescription2{
                    .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                    .pNext          = nullptr,
                    .flags          = 0,
                    .format         = resolve_format,
                    .samples        = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout    = to_vk_image_layout(att.layout_after)
                });
                ERHE_VULKAN_SYNC_TRACE(
                    "[RP_ATTACHMENT] pass=\"{}\" role=color{}_resolve tex=\"{}\" samples=1 loadOp=DONT_CARE storeOp=STORE stencilLoadOp=DONT_CARE stencilStoreOp=DONT_CARE initialLayout=UNDEFINED finalLayout={}",
                    m_debug_label.data(), i,
                    att.resolve_texture->get_debug_label().data(),
                    vk_layout_str(to_vk_image_layout(att.layout_after))
                );
                resolve_attachment_references.push_back(VkAttachmentReference2{
                    .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                    .pNext      = nullptr,
                    .attachment = attachment_index,
                    .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
                });
                VkImageView resolve_view = const_cast<Texture_impl&>(att.resolve_texture->get_impl()).get_vk_image_view(
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
                );
                image_views.push_back(resolve_view);
                m_clear_values.push_back(VkClearValue{}); // resolve doesn't use clear
                ++attachment_index;
            } else {
                resolve_attachment_references.push_back(VkAttachmentReference2{
                    .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                    .pNext      = nullptr,
                    .attachment = VK_ATTACHMENT_UNUSED,
                    .layout     = VK_IMAGE_LAYOUT_UNDEFINED,
                    .aspectMask = 0
                });
            }
        }

        // Depth attachment
        VkAttachmentReference2 depth_resolve_reference{
            .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .pNext      = nullptr,
            .attachment = VK_ATTACHMENT_UNUSED,
            .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT
        };
        bool                   has_depth_resolve = false;
        VkResolveModeFlagBits  depth_resolve_mode_vk   = VK_RESOLVE_MODE_NONE;
        VkResolveModeFlagBits  stencil_resolve_mode_vk = VK_RESOLVE_MODE_NONE;
        if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
            VkFormat depth_format = to_vulkan(m_depth_attachment.texture->get_pixelformat());

            // Use stencil load/store from stencil_attachment if it references the same texture
            const bool has_stencil = (m_stencil_attachment.texture == m_depth_attachment.texture) && m_stencil_attachment.is_defined();
            const bool fresh_depth = (m_depth_attachment.texture->get_impl().get_current_layout() == VK_IMAGE_LAYOUT_UNDEFINED);
            // LOAD with UNDEFINED initial layout is invalid (VUID-06699 applies
            // to depth and stencil as well). Fall back to the declared
            // layout_before when either depth or stencil will be loaded.
            const bool depth_wants_load   = (m_depth_attachment.load_action == Load_action::Load);
            const bool stencil_wants_load = has_stencil && (m_stencil_attachment.load_action == Load_action::Load);
            const VkImageLayout depth_initial_layout = (fresh_depth && !depth_wants_load && !stencil_wants_load)
                ? VK_IMAGE_LAYOUT_UNDEFINED
                : to_vk_image_layout(m_depth_attachment.layout_before);
            // When the depth attachment is being resolved, the MSAA source
            // image is going to be discarded -- its finalLayout doesn't matter
            // to the user. Keep it in DEPTH_STENCIL_ATTACHMENT_OPTIMAL so we
            // don't pay for an end-of-pass layout transition. The resolve
            // target's finalLayout (set below) is what the user's layout_after
            // controls.
            const bool has_depth_resolve_request =
                (m_depth_attachment.resolve_texture != nullptr) &&
                ((m_depth_attachment.store_action == Store_action::Multisample_resolve) ||
                 (m_depth_attachment.store_action == Store_action::Store_and_multisample_resolve));
            const VkImageLayout depth_final_layout   = has_depth_resolve_request
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                : to_vk_image_layout(m_depth_attachment.layout_after);

            // Multisampled depth/stencil attachments: same rule as color --
            // store ops are forced DONT_CARE unless the user requested a
            // multisample resolve. Store_and_multisample_resolve preserves the
            // MSAA samples in addition to the resolve target.
            const VkSampleCountFlagBits depth_samples = get_vulkan_sample_count(m_depth_attachment.texture->get_sample_count());
            const bool depth_msaa = (depth_samples != VK_SAMPLE_COUNT_1_BIT);

            const bool depth_resolves =
                (m_depth_attachment.resolve_texture != nullptr) &&
                ((m_depth_attachment.store_action == Store_action::Multisample_resolve) ||
                 (m_depth_attachment.store_action == Store_action::Store_and_multisample_resolve));
            const bool stencil_resolves = has_stencil &&
                (m_stencil_attachment.resolve_texture != nullptr) &&
                ((m_stencil_attachment.store_action == Store_action::Multisample_resolve) ||
                 (m_stencil_attachment.store_action == Store_action::Store_and_multisample_resolve));

            VkAttachmentLoadOp  depth_load_op    = to_vk_load_op(m_depth_attachment.load_action);
            VkAttachmentStoreOp depth_store_op   = to_vk_store_op(m_depth_attachment.store_action);
            VkAttachmentLoadOp  stencil_load_op  = has_stencil ? to_vk_load_op(m_stencil_attachment.load_action) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp stencil_store_op = has_stencil ? to_vk_store_op(m_stencil_attachment.store_action) : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            if (depth_msaa) {
                // MSAA depth/stencil: only force DONT_CARE when the caller
                // didn't explicitly ask to preserve the multisampled
                // contents (Store or Store_and_multisample_resolve). A plain
                // Store means "preserve the multisampled samples" -- needed
                // for e.g. multi-render-pass splits that want to Load
                // stencil in a follow-up pass. Forcing DONT_CARE here was
                // a premature optimization that silently dropped the data.
                if ((depth_load_op == VK_ATTACHMENT_LOAD_OP_LOAD) && (m_depth_attachment.load_action != Load_action::Load)) {
                    depth_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                }
                if ((m_depth_attachment.store_action == Store_action::Store) ||
                    (m_depth_attachment.store_action == Store_action::Store_and_multisample_resolve)) {
                    depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;
                } else {
                    depth_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }
                if ((stencil_load_op == VK_ATTACHMENT_LOAD_OP_LOAD) && (!has_stencil || m_stencil_attachment.load_action != Load_action::Load)) {
                    stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                }
                if (has_stencil &&
                    ((m_stencil_attachment.store_action == Store_action::Store) ||
                     (m_stencil_attachment.store_action == Store_action::Store_and_multisample_resolve))) {
                    stencil_store_op = VK_ATTACHMENT_STORE_OP_STORE;
                } else {
                    stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }
            }
            if ((depth_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) || (stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)) {
                m_any_load_op_clear = true;
            }
            attachment_descriptions.push_back(VkAttachmentDescription2{
                .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .pNext          = nullptr,
                .flags          = 0,
                .format         = depth_format,
                .samples        = depth_samples,
                .loadOp         = depth_load_op,
                .storeOp        = depth_store_op,
                .stencilLoadOp  = stencil_load_op,
                .stencilStoreOp = stencil_store_op,
                .initialLayout  = depth_initial_layout,
                .finalLayout    = depth_final_layout
            });
            ERHE_VULKAN_SYNC_TRACE(
                "[RP_ATTACHMENT] pass=\"{}\" role=depth_stencil tex=\"{}\" samples={} loadOp={} storeOp={} stencilLoadOp={} stencilStoreOp={} initialLayout={} finalLayout={}",
                m_debug_label.data(),
                m_depth_attachment.texture->get_debug_label().data(),
                static_cast<int>(depth_samples),
                vk_load_op_str(depth_load_op),
                vk_store_op_str(depth_store_op),
                vk_load_op_str(stencil_load_op),
                vk_store_op_str(stencil_store_op),
                vk_layout_str(depth_initial_layout),
                vk_layout_str(depth_final_layout)
            );

            // Framebuffer attachments require single mip level views
            const erhe::dataformat::Format depth_pixelformat = m_depth_attachment.texture->get_pixelformat();
            VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (erhe::dataformat::get_stencil_size_bits(depth_pixelformat) > 0) {
                depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            depth_stencil_reference.attachment = attachment_index;
            depth_stencil_reference.aspectMask = depth_aspect;
            has_depth_stencil = true;

            VkImageView view = const_cast<Texture_impl&>(m_depth_attachment.texture->get_impl()).get_vk_image_view(
                depth_aspect,
                static_cast<uint32_t>(m_depth_attachment.texture_layer),
                1,
                static_cast<uint32_t>(m_depth_attachment.texture_level),
                1
            );
            image_views.push_back(view);

            VkClearValue clear_value{};
            clear_value.depthStencil = VkClearDepthStencilValue{
                .depth   = static_cast<float>(m_depth_attachment.clear_value[0]),
                .stencil = has_stencil ? static_cast<uint32_t>(m_stencil_attachment.clear_value[0]) : 0u
            };
            m_clear_values.push_back(clear_value);

            ++attachment_index;

            // Depth/stencil resolve attachment.
            //
            // We append a single-sample VkAttachmentDescription2 for the resolve
            // target and chain a VkSubpassDescriptionDepthStencilResolve into the
            // SubpassDescription2 below. The resolve target's image view picks up
            // both depth and stencil aspects when the format has stencil bits, so
            // a single resolve attachment serves both depth and stencil resolves.
            if (depth_resolves || stencil_resolves) {
                ERHE_VERIFY(depth_msaa); // resolves are only meaningful for MSAA
                const Texture* resolve_texture = (m_depth_attachment.resolve_texture != nullptr)
                    ? m_depth_attachment.resolve_texture
                    : m_stencil_attachment.resolve_texture;
                ERHE_VERIFY(resolve_texture != nullptr);
                ERHE_VERIFY(resolve_texture->get_sample_count() <= 1);

                const erhe::dataformat::Format resolve_pixelformat = resolve_texture->get_pixelformat();
                VkImageAspectFlags resolve_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                if (erhe::dataformat::get_stencil_size_bits(resolve_pixelformat) > 0) {
                    resolve_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
                attachment_descriptions.push_back(VkAttachmentDescription2{
                    .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                    .pNext          = nullptr,
                    .flags          = 0,
                    .format         = to_vulkan(resolve_pixelformat),
                    .samples        = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .stencilStoreOp = (resolve_aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                    // The resolve target picks up the user's intended
                    // post-pass layout via depth_attachment.layout_after, so
                    // sampling the resolved depth in the next pass works
                    // without an explicit barrier.
                    .finalLayout    = to_vk_image_layout(m_depth_attachment.layout_after)
                });
                ERHE_VULKAN_SYNC_TRACE(
                    "[RP_ATTACHMENT] pass=\"{}\" role=depth_stencil_resolve tex=\"{}\" samples=1 loadOp=DONT_CARE storeOp=STORE stencilLoadOp=DONT_CARE stencilStoreOp={} initialLayout=UNDEFINED finalLayout={}",
                    m_debug_label.data(),
                    resolve_texture->get_debug_label().data(),
                    vk_store_op_str((resolve_aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE),
                    vk_layout_str(to_vk_image_layout(m_depth_attachment.layout_after))
                );
                depth_resolve_reference.attachment = attachment_index;
                depth_resolve_reference.aspectMask = resolve_aspect;
                has_depth_resolve = true;
                depth_resolve_mode_vk   = depth_resolves   ? to_vk_resolve_mode(m_depth_attachment.resolve_mode)   : VK_RESOLVE_MODE_NONE;
                stencil_resolve_mode_vk = stencil_resolves ? to_vk_resolve_mode(m_stencil_attachment.resolve_mode) : VK_RESOLVE_MODE_NONE;

                VkImageView resolve_view = const_cast<Texture_impl&>(resolve_texture->get_impl()).get_vk_image_view(
                    resolve_aspect, 0, 1, 0, 1
                );
                image_views.push_back(resolve_view);
                m_clear_values.push_back(VkClearValue{}); // resolve doesn't use clear

                ++attachment_index;
            }
        }

        // VkSubpassDescriptionDepthStencilResolve is chained into pNext only if
        // the user requested a depth or stencil resolve. The struct must remain
        // alive through the vkCreateRenderPass2 call below.
        const VkSubpassDescriptionDepthStencilResolve depth_stencil_resolve_info{
            .sType                          = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE,
            .pNext                          = nullptr,
            .depthResolveMode               = depth_resolve_mode_vk,
            .stencilResolveMode             = stencil_resolve_mode_vk,
            .pDepthStencilResolveAttachment = &depth_resolve_reference
        };
        const VkSubpassDescription2 subpass_description{
            .sType                   = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
            .pNext                   = has_depth_resolve ? &depth_stencil_resolve_info : nullptr,
            .flags                   = 0,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .viewMask                = 0,
            .inputAttachmentCount    = 0,
            .pInputAttachments       = nullptr,
            .colorAttachmentCount    = static_cast<uint32_t>(color_attachment_references.size()),
            .pColorAttachments       = color_attachment_references.empty() ? nullptr : color_attachment_references.data(),
            .pResolveAttachments     = has_resolve ? resolve_attachment_references.data() : nullptr,
            .pDepthStencilAttachment = has_depth_stencil ? &depth_stencil_reference : nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments    = nullptr
        };

        // Use canonical dependencies derived only from attachment structure so
        // they exactly match the pipeline's compatibility render pass.
        const bool has_color_attachments = !color_attachment_references.empty();
        VkSubpassDependency2 canonical_dependencies[2];
        make_canonical_subpass_dependencies2(has_color_attachments, has_depth_stencil, canonical_dependencies);
        const VkRenderPassCreateInfo2 render_pass_create_info{
            .sType                   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
            .pNext                   = nullptr,
            .flags                   = 0,
            .attachmentCount         = static_cast<uint32_t>(attachment_descriptions.size()),
            .pAttachments            = attachment_descriptions.empty() ? nullptr : attachment_descriptions.data(),
            .subpassCount            = 1,
            .pSubpasses              = &subpass_description,
            .dependencyCount         = 2,
            .pDependencies           = canonical_dependencies,
            .correlatedViewMaskCount = 0,
            .pCorrelatedViewMasks    = nullptr
        };

        result = vkCreateRenderPass2(vulkan_device, &render_pass_create_info, nullptr, &m_render_pass);
        if (result != VK_SUCCESS) {
            log_render_pass->critical("vkCreateRenderPass2() failed for off-screen pass with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Create framebuffer
        const VkFramebufferCreateInfo framebuffer_create_info{
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .renderPass      = m_render_pass,
            .attachmentCount = static_cast<uint32_t>(image_views.size()),
            .pAttachments    = image_views.empty() ? nullptr : image_views.data(),
            .width           = static_cast<uint32_t>(m_render_target_width),
            .height          = static_cast<uint32_t>(m_render_target_height),
            .layers          = 1
        };

        result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &m_framebuffer);
        if (result != VK_SUCCESS) {
            log_render_pass->critical("vkCreateFramebuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Command buffers for this render pass come from the device's
        // per-frame-in-flight pools; no per-Render_pass_impl pool is needed.

        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_RENDER_PASS,
            reinterpret_cast<uint64_t>(m_render_pass),
            fmt::format("Render_pass_impl off-screen: {}", descriptor.debug_label.string_view())
        );

        // Pre-transition of attachment textures to their layout_before is
        // deferred to the first start_render_pass(). Doing it here would
        // require an immediate-command submit (no Command_buffer is
        // available at construction); start_render_pass already has the
        // caller-supplied cb to record into and is the natural place to
        // emit any one-shot UNDEFINED -> layout_before barrier the render
        // pass machinery itself can't drive.

        log_render_pass->debug(
            "Off-screen render pass created: {}x{}, {} color attachments, depth={}",
            m_render_target_width, m_render_target_height,
            color_attachment_references.size(),
            has_depth_stencil ? "yes" : "no"
        );
    }
}


Render_pass_impl::~Render_pass_impl() noexcept
{
    // Defer destruction via a completion handler so the handles remain
    // valid while the device-frame command buffer is still in
    // recording state. The handler runs in frame_completed() for the
    // current frame, after the GPU has finished with it. Same pattern
    // as Texture_impl.
    const VkFramebuffer framebuffer = m_framebuffer;
    const VkRenderPass  render_pass = m_render_pass;
    m_framebuffer = VK_NULL_HANDLE;
    m_render_pass = VK_NULL_HANDLE;

    m_device_impl.add_completion_handler(
        [framebuffer, render_pass](Device_impl& device_impl) {
            VkDevice vulkan_device = device_impl.get_vulkan_device();
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(vulkan_device, framebuffer, nullptr);
            }
            if (render_pass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(vulkan_device, render_pass, nullptr);
            }
        }
    );
}

auto Render_pass_impl::get_swapchain() const -> Swapchain*
{
    return m_swapchain;
}

void Render_pass_impl::reset()
{
}

void Render_pass_impl::create()
{
}

auto Render_pass_impl::get_color_attachment_count() const -> unsigned int
{
    if (m_swapchain != nullptr) {
        return 1;
    }
    unsigned int count = 0;
    for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
        if (att.is_defined()) {
            ++count;
        }
    }
    return count;
}

auto Render_pass_impl::get_sample_count() const -> unsigned int
{
    for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
        if (att.is_defined() && (att.texture != nullptr)) {
            const int sample_count = att.texture->get_sample_count();
            if (sample_count > 0) {
                return static_cast<unsigned int>(sample_count);
            }
        }
    }
    if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
        const int sample_count = m_depth_attachment.texture->get_sample_count();
        if (sample_count > 0) {
            return static_cast<unsigned int>(sample_count);
        }
    }
    return 1;
}

auto Render_pass_impl::check_status() const -> bool
{
    return m_render_pass != VK_NULL_HANDLE;
}

auto Render_pass_impl::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass_impl::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Render_pass_impl::start_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_before, Render_pass* const render_pass_after)
{
    // command_buffer is the explicit Command_buffer this pass should
    // record into. Iteration target: pull the underlying VkCommandBuffer
    // out of command_buffer.get_impl().get_vulkan_command_buffer() and use
    // that for vkCmdBeginRenderPass2 below instead of the device-frame
    // active cb.
    //
    // render_pass_before: used to narrow the source side of the inter-pass
    // memory barrier when the predecessor is known (e.g. post-processing
    // chains). When nullptr, the barrier uses a conservative source mask.
    //
    // render_pass_after: used for debug-time validation only (see
    // end_render_pass).
    static_cast<void>(render_pass_before);
    static_cast<void>(render_pass_after);

    // Deferred pre-transitions: if any attachment texture is in a
    // layout other than its layout_before (and not still UNDEFINED,
    // since the render pass machinery handles UNDEFINED itself), emit
    // the transitions into the caller-supplied cb before the render
    // pass begins. Used to live in the constructor as an immediate-
    // commands submit; moved here so all GPU recording goes through
    // an explicit cb.
    {
        const VkCommandBuffer pre_cb = command_buffer.get_impl().get_vulkan_command_buffer();
        if (pre_cb != VK_NULL_HANDLE) {
            auto needs_transition = [](const Render_pass_attachment_descriptor& att, const VkImageLayout target) {
                const VkImageLayout current = att.texture->get_impl().get_current_layout();
                // Skip transitioning when:
                //  - texture is still UNDEFINED (the render pass machinery
                //    handles UNDEFINED initialLayout itself),
                //  - target is UNDEFINED (descriptor expressed "don't care
                //    about prior contents" -- transitioning to UNDEFINED
                //    is invalid per VUID-VkImageMemoryBarrier2-newLayout-01198),
                //  - we are already in the target layout.
                return (current != VK_IMAGE_LAYOUT_UNDEFINED) &&
                       (target  != VK_IMAGE_LAYOUT_UNDEFINED) &&
                       (current != target);
            };
            for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
                if (!att.is_defined() || (att.texture == nullptr)) {
                    continue;
                }
                const VkImageLayout target_layout = to_vk_image_layout(att.layout_before);
                if (needs_transition(att, target_layout)) {
                    att.texture->get_impl().transition_layout(pre_cb, target_layout);
                }
            }
            if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
                const VkImageLayout target_layout = to_vk_image_layout(m_depth_attachment.layout_before);
                if (needs_transition(m_depth_attachment, target_layout)) {
                    m_depth_attachment.texture->get_impl().transition_layout(pre_cb, target_layout);
                }
            }
        }
    }

    m_device_impl.set_active_render_pass_impl(this);

    // Log implicit layout transitions performed by the render pass
    log_vulkan->trace("render_pass '{}' begin", m_debug_label.data());
    for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
        if (!att.is_defined() || (att.texture == nullptr)) {
            continue;
        }
        const VkImageLayout from = to_vk_image_layout(att.layout_before);
        const VkImageLayout to   = (att.resolve_texture != nullptr)
            ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            : to_vk_image_layout(att.layout_after);
        VkImageMemoryBarrier2 b{};
        b.image     = att.texture->get_impl().get_vk_image();
        b.oldLayout = from;
        b.newLayout = to;
        log_image_layout_transition(b, "render_pass color");
    }
    if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
        const bool has_depth_resolve_request =
            (m_depth_attachment.resolve_texture != nullptr) &&
            ((m_depth_attachment.store_action == Store_action::Multisample_resolve) ||
             (m_depth_attachment.store_action == Store_action::Store_and_multisample_resolve));
        const VkImageLayout from = to_vk_image_layout(m_depth_attachment.layout_before);
        const VkImageLayout to   = has_depth_resolve_request
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : to_vk_image_layout(m_depth_attachment.layout_after);
        VkImageMemoryBarrier2 b{};
        b.image     = m_depth_attachment.texture->get_impl().get_vk_image();
        b.oldLayout = from;
        b.newLayout = to;
        log_image_layout_transition(b, "render_pass depth");
    }

    if (m_swapchain != nullptr) {
        // Swapchain render pass. Record into the user-supplied cb;
        // Swapchain_impl just supplies the framebuffer for the
        // currently-acquired image and the swapchain extent.
        m_command_buffer = command_buffer.get_impl().get_vulkan_command_buffer();
        if (m_command_buffer == VK_NULL_HANDLE) {
            log_render_pass->error("Command_buffer has no VkCommandBuffer bound (swapchain pass)");
            m_device_impl.set_active_render_pass_impl(nullptr);
            return;
        }

        Swapchain_impl& swapchain_impl = m_swapchain->get_impl();
        VkFramebuffer   framebuffer    = swapchain_impl.acquire_swapchain_framebuffer(m_render_pass);
        if (framebuffer == VK_NULL_HANDLE) {
            log_render_pass->error("acquire_swapchain_framebuffer() returned VK_NULL_HANDLE");
            m_device_impl.set_active_render_pass_impl(nullptr);
            return;
        }

        const VkRenderPassBeginInfo render_pass_begin_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = m_render_pass,
            .framebuffer     = framebuffer,
            .renderArea      = VkRect2D{{0, 0}, swapchain_impl.get_swapchain_extent()},
            .clearValueCount = m_any_load_op_clear ? static_cast<uint32_t>(m_clear_values.size()) : 0u,
            .pClearValues    = m_any_load_op_clear ? m_clear_values.data() : nullptr
        };
        const VkSubpassBeginInfo subpass_begin_info{
            .sType    = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
            .pNext    = nullptr,
            .contents = VK_SUBPASS_CONTENTS_INLINE
        };
        log_render_pass->trace(
            "start_render_pass swapchain branch: vkCmdBeginRenderPass2 cb=0x{:x} fb=0x{:x} extent={}x{}",
            reinterpret_cast<std::uintptr_t>(m_command_buffer),
            reinterpret_cast<std::uintptr_t>(framebuffer),
            render_pass_begin_info.renderArea.extent.width,
            render_pass_begin_info.renderArea.extent.height
        );
        vkCmdBeginRenderPass2(m_command_buffer, &render_pass_begin_info, &subpass_begin_info);
        swapchain_impl.mark_render_pass_recorded();
    } else {
        // Non-swapchain render pass. Record into the explicit
        // Command_buffer the caller handed to start_render_pass. The
        // cb must already be in recording state (caller has called
        // begin()); we just pull its underlying VkCommandBuffer here.
        m_command_buffer = command_buffer.get_impl().get_vulkan_command_buffer();
        if (m_command_buffer == VK_NULL_HANDLE) {
            log_render_pass->error("Command_buffer has no VkCommandBuffer bound");
            m_device_impl.set_active_render_pass_impl(nullptr);
            return;
        }

        // Inter-pass memory barrier derived entirely from THIS pass's
        // attachment descriptors:
        //
        // Source side: each attachment's usage_before describes how the
        // resource was used prior to this pass. Converted to Vulkan
        // stage/access via usage_to_vk_stage_access().
        //
        // Destination side: derived from which attachment types exist.
        // FRAGMENT_SHADER + SHADER_READ always included for descriptor-
        // bound texture sampling.
        //
        // Compute dispatches and immediate-command uploads use separate
        // command pools with synchronous submit+wait, so they do not
        // need coverage here. Ring-buffer uniform/vertex data uses
        // host-coherent persistent mapping; host writes are made
        // visible to the device domain by vkQueueSubmit (Path A: per
        // pass; merged_into_device_frame: end of frame). Layout
        // transitions remain driven by VkRenderPass initialLayout /
        // finalLayout.
        {
            const VkMemoryBarrier2 memory_barrier = compute_inter_pass_barrier(
                m_color_attachments,
                m_depth_attachment,
                m_stencil_attachment
            );
            const VkDependencyInfo dependency_info{
                .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext                    = nullptr,
                .dependencyFlags          = 0,
                .memoryBarrierCount       = 1,
                .pMemoryBarriers          = &memory_barrier,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers    = nullptr,
                .imageMemoryBarrierCount  = 0,
                .pImageMemoryBarriers     = nullptr,
            };
            vkCmdPipelineBarrier2(m_command_buffer, &dependency_info);

            ERHE_VULKAN_SYNC_TRACE(
                "[BARRIER] pass=\"{}\" src_stage={} src_access={} dst_stage={} dst_access={} predecessor=\"{}\"",
                m_debug_label.data(),
                pipeline_stage_flags_str(memory_barrier.srcStageMask),
                access_flags_str(memory_barrier.srcAccessMask),
                pipeline_stage_flags_str(memory_barrier.dstStageMask),
                access_flags_str(memory_barrier.dstAccessMask),
                (render_pass_before != nullptr) ? render_pass_before->get_debug_label().data() : "(none)"
            );

            auto log_att = [&](const Render_pass_attachment_descriptor& att, const char* role) {
                if (!att.is_defined() || (att.texture == nullptr)) {
                    return;
                }
                ERHE_VULKAN_SYNC_TRACE(
                    "[BARRIER_ATT] pass=\"{}\" role={} tex=\"{}\" usage_before={} usage_after={} layout_before={} layout_after={}",
                    m_debug_label.data(),
                    role,
                    att.texture->get_debug_label().data(),
                    image_usage_mask_str(att.usage_before),
                    image_usage_mask_str(att.usage_after),
                    image_layout_str(att.layout_before),
                    image_layout_str(att.layout_after)
                );
                static_cast<void>(role);
            };
            for (std::size_t i = 0; i < m_color_attachments.size(); ++i) {
                const std::string role = fmt::format("color{}", i);
                log_att(m_color_attachments[i], role.c_str());
            }
            log_att(m_depth_attachment,   "depth");
            log_att(m_stencil_attachment, "stencil");
        }

        // Begin render pass
        const VkRenderPassBeginInfo render_pass_begin_info{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = m_render_pass,
            .framebuffer     = m_framebuffer,
            .renderArea      = VkRect2D{
                .offset = {0, 0},
                .extent = {
                    static_cast<uint32_t>(m_render_target_width),
                    static_cast<uint32_t>(m_render_target_height)
                }
            },
            .clearValueCount = m_any_load_op_clear ? static_cast<uint32_t>(m_clear_values.size()) : 0u,
            .pClearValues    = m_any_load_op_clear ? m_clear_values.data() : nullptr
        };
        const VkSubpassBeginInfo subpass_begin_info{
            .sType    = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
            .pNext    = nullptr,
            .contents = VK_SUBPASS_CONTENTS_INLINE
        };
        vkCmdBeginRenderPass2(m_command_buffer, &render_pass_begin_info, &subpass_begin_info);

        {
            int color_count = 0;
            for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
                if (att.is_defined() && (att.texture != nullptr)) {
                    ++color_count;
                }
            }
            const bool has_depth   = m_depth_attachment.is_defined()   && (m_depth_attachment.texture   != nullptr);
            const bool has_stencil = m_stencil_attachment.is_defined() && (m_stencil_attachment.texture != nullptr);
            static_cast<void>(has_depth);
            static_cast<void>(has_stencil);
            ERHE_VULKAN_SYNC_TRACE(
                "[RP_BEGIN] pass=\"{}\" color={} depth={} stencil={} area={}x{}",
                m_debug_label.data(),
                color_count,
                has_depth,
                has_stencil,
                m_render_target_width,
                m_render_target_height
            );
            ERHE_VULKAN_DESC_TRACE(
                "[RP_BEGIN] pass=\"{}\" color={} depth={} stencil={} area={}x{}",
                m_debug_label.data(),
                color_count,
                has_depth,
                has_stencil,
                m_render_target_width,
                m_render_target_height
            );
        }
    }
}

void Render_pass_impl::end_render_pass(Render_pass* const render_pass_after)
{
#ifndef NDEBUG
    // Debug-only consistency check: whenever the caller has declared a
    // successor render pass, any attachment texture that appears in both
    // descriptors must have matching usage_after (here) and usage_before
    // (there). A mismatch indicates a rendergraph wiring bug that would
    // otherwise manifest as a Vulkan validation-layer warning at the next
    // pass's begin.
    if (render_pass_after != nullptr) {
        const Render_pass_descriptor& next_descriptor = render_pass_after->get_descriptor();
        auto check_attachment = [&](const Render_pass_attachment_descriptor& here, const char* which) {
            if (!here.is_defined() || (here.texture == nullptr)) {
                return;
            }
            auto scan = [&](const Render_pass_attachment_descriptor& there, const char* there_which) {
                if (!there.is_defined() || (there.texture != here.texture)) {
                    return;
                }
                if (here.layout_after != there.layout_before) {
                    log_render_pass->warn(
                        "Render_pass '{}' {}.layout_after={} does not match successor Render_pass '{}' {}.layout_before={} for the same texture",
                        m_debug_label.data(), which,  static_cast<unsigned int>(here.layout_after),
                        render_pass_after->get_debug_label().data(), there_which, static_cast<unsigned int>(there.layout_before)
                    );
                }
            };
            for (std::size_t i = 0; i < next_descriptor.color_attachments.size(); ++i) {
                scan(next_descriptor.color_attachments[i], "color_attachment");
            }
            scan(next_descriptor.depth_attachment,   "depth_attachment");
            scan(next_descriptor.stencil_attachment, "stencil_attachment");
        };
        for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
            check_attachment(att, "color_attachment");
        }
        check_attachment(m_depth_attachment,   "depth_attachment");
        check_attachment(m_stencil_attachment, "stencil_attachment");
    }
#else
    static_cast<void>(render_pass_after);
#endif

    if (m_swapchain != nullptr) {
        // Mirror of the swapchain branch in start_render_pass: record
        // vkCmdEndRenderPass2 directly into the user cb. Submit
        // remains the responsibility of Device::submit_command_buffers.
        if (m_command_buffer != VK_NULL_HANDLE) {
            const VkSubpassEndInfo subpass_end_info{
                .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
                .pNext = nullptr
            };
            log_render_pass->trace(
                "end_render_pass swapchain branch: vkCmdEndRenderPass2 cb=0x{:x}",
                reinterpret_cast<std::uintptr_t>(m_command_buffer)
            );
            vkCmdEndRenderPass2(m_command_buffer, &subpass_end_info);
        }
    } else {
        // End the render pass. The device-frame command buffer stays
        // open and is finalized by Device_impl::end_frame.
        if (m_command_buffer != VK_NULL_HANDLE) {
            const VkSubpassEndInfo subpass_end_info{
                .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
                .pNext = nullptr
            };
            vkCmdEndRenderPass2(m_command_buffer, &subpass_end_info);

            // Option A: post-renderpass barrier. Makes attachment writes
            // visible for the usage_after stages (e.g. FRAGMENT_SHADER
            // when usage_after = sampled). Without this, a subsequent
            // pass that samples from a texture written as a color
            // attachment would have a synchronization hazard: the
            // subpass 0->EXTERNAL dependency only extends visibility to
            // COLOR_ATTACHMENT_OUTPUT, not to FRAGMENT_SHADER.
            //
            // Skipped for attachments with the user_synchronized bit in
            // usage_after (Option B: caller manages barriers explicitly).
            {
                VkMemoryBarrier2 post_barrier{};
                const bool need_barrier = compute_post_render_pass_barrier(
                    m_color_attachments,
                    m_depth_attachment,
                    m_stencil_attachment,
                    post_barrier
                );
                if (need_barrier) {
                    const VkDependencyInfo dependency_info{
                        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        .pNext                    = nullptr,
                        .dependencyFlags          = 0,
                        .memoryBarrierCount       = 1,
                        .pMemoryBarriers          = &post_barrier,
                        .bufferMemoryBarrierCount = 0,
                        .pBufferMemoryBarriers    = nullptr,
                        .imageMemoryBarrierCount  = 0,
                        .pImageMemoryBarriers     = nullptr,
                    };
                    vkCmdPipelineBarrier2(m_command_buffer, &dependency_info);

                    ERHE_VULKAN_SYNC_TRACE(
                        "[POST_RP_BARRIER] pass=\"{}\" src_stage={} src_access={} dst_stage={} dst_access={}",
                        m_debug_label.data(),
                        pipeline_stage_flags_str(post_barrier.srcStageMask),
                        access_flags_str(post_barrier.srcAccessMask),
                        pipeline_stage_flags_str(post_barrier.dstStageMask),
                        access_flags_str(post_barrier.dstAccessMask)
                    );
                }
            }

            ERHE_VULKAN_SYNC_TRACE("[RP_END] pass=\"{}\"", m_debug_label.data());
            ERHE_VULKAN_DESC_TRACE("[RP_END] pass=\"{}\"", m_debug_label.data());
        }

        // Update tracked layout to match the render pass finalLayout
        for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
            if (att.is_defined() && (att.texture != nullptr)) {
                const VkImageLayout final_layout = (att.resolve_texture != nullptr)
                    ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                    : to_vk_image_layout(att.layout_after);
                const_cast<Texture_impl&>(att.texture->get_impl()).set_layout(final_layout);
                if (att.resolve_texture != nullptr) {
                    const VkImageLayout resolve_final_layout = to_vk_image_layout(att.layout_after);
                    const_cast<Texture_impl&>(att.resolve_texture->get_impl()).set_layout(resolve_final_layout);
                }
            }
        }
        if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
            const bool has_depth_resolve_request =
                (m_depth_attachment.resolve_texture != nullptr) &&
                ((m_depth_attachment.store_action == Store_action::Multisample_resolve) ||
                 (m_depth_attachment.store_action == Store_action::Store_and_multisample_resolve));
            // Source MSAA depth ends up in DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            // (matching the finalLayout we set on the SOURCE attachment when a
            // resolve was requested) or in usage_after when there was none.
            const VkImageLayout source_layout = has_depth_resolve_request
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                : to_vk_image_layout(m_depth_attachment.layout_after);
            const_cast<Texture_impl&>(m_depth_attachment.texture->get_impl()).set_layout(source_layout);

            if (has_depth_resolve_request) {
                const VkImageLayout resolve_final_layout = to_vk_image_layout(m_depth_attachment.layout_after);
                const_cast<Texture_impl&>(m_depth_attachment.resolve_texture->get_impl()).set_layout(resolve_final_layout);
            }
        }
    }

    m_command_buffer = VK_NULL_HANDLE;
    m_device_impl.set_active_render_pass_impl(nullptr);
}

} // namespace erhe::graphics
