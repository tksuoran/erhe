#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_scoped_debug_group.hpp"
#include "erhe_graphics/metal/metal_surface.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/metal/metal_swapchain.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cstring>
#include "erhe_utility/align.hpp"

#include <fmt/format.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

namespace erhe::graphics {

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config)
    : m_device         {device}
    , m_shader_monitor {device}
    , m_graphics_config{graphics_config}
{

    install_metal_error_handler(device);

    m_mtl_device = MTL::CreateSystemDefaultDevice();
    if (m_mtl_device == nullptr) {
        log_startup->error("Failed to create Metal device");
        return;
    }

    log_startup->info("Metal device: {}", m_mtl_device->name()->utf8String());

    // Require Metal 3 (GPU family Metal3): the bindless texture heap relies on
    // SPIRV-Cross MSL 3+ for full mutable aliasing of argument buffer descriptors.
    if (!m_mtl_device->supportsFamily(MTL::GPUFamilyMetal3)) {
        log_startup->critical(
            "Metal device '{}' does not support GPUFamilyMetal3 (Metal 3). "
            "erhe requires Metal 3.",
            m_mtl_device->name()->utf8String()
        );
        abort();
    }

    m_mtl_command_queue = m_mtl_device->newCommandQueue();

    // Metal LogState with custom log handler is disabled for now because it
    // interferes with Metal GPU capture (Xcode frame debugger).
    // TODO Re-enable when not using GPU capture, or make it configurable.
#if 0
    // Create log state for Metal validation message capture
    {
        NS::Error* log_error = nullptr;
        MTL::LogStateDescriptor* log_desc = MTL::LogStateDescriptor::alloc()->init();
        log_desc->setLevel(MTL::LogLevelDebug);
        m_mtl_log_state = m_mtl_device->newLogState(log_desc, &log_error);
        log_desc->release();
        if (m_mtl_log_state != nullptr) {
            Device* device_ptr = &device;
            m_mtl_log_state->addLogHandler(
                [device_ptr](NS::String* subsystem, NS::String* category, MTL::LogLevel level, NS::String* message) {
                    const char* subsystem_str = (subsystem != nullptr) ? subsystem->utf8String() : "";
                    const char* category_str  = (category  != nullptr) ? category->utf8String()  : "";
                    const char* message_str   = (message   != nullptr) ? message->utf8String()   : "";
                    const char* level_str =
                        (level == MTL::LogLevelFault)  ? "FAULT"  :
                        (level == MTL::LogLevelError)  ? "ERROR"  :
                        (level == MTL::LogLevelNotice) ? "NOTICE" :
                        (level == MTL::LogLevelInfo)   ? "INFO"   :
                        (level == MTL::LogLevelDebug)  ? "DEBUG"  : "?";
                    std::string full_message = fmt::format(
                        "Metal [{}] {}/{}: {}",
                        level_str, subsystem_str, category_str, message_str
                    );
                    log_startup->info("{}", full_message);
                    device_ptr->trace(full_message);
                }
            );
            log_startup->info("Metal log state created with log handler");
        } else {
            const char* err_str = (log_error != nullptr) ? log_error->localizedDescription()->utf8String() : "unknown";
            log_startup->warn("Failed to create Metal log state: {}", err_str);
        }
    }
#endif

    // Create default sampler state
    {
        MTL::SamplerDescriptor* sampler_desc = MTL::SamplerDescriptor::alloc()->init();
        sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
        sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampler_desc->setMipFilter(MTL::SamplerMipFilterLinear);
        sampler_desc->setSupportArgumentBuffers(true);
        m_default_sampler_state = m_mtl_device->newSamplerState(sampler_desc);
        sampler_desc->release();
    }
    if (m_mtl_command_queue == nullptr) {
        log_startup->error("Failed to create Metal command queue");
        return;
    }

    // Query max supported multisample count from device
    int max_samples = 1;
    for (int count : {8, 4, 2}) {
        if (m_mtl_device->supportsTextureSampleCount(static_cast<NS::UInteger>(count))) {
            max_samples = count;
            break;
        }
    }
    log_startup->info("Metal max supported sample count: {}", max_samples);

    const MTL::Size max_threads = m_mtl_device->maxThreadsPerThreadgroup();

    m_info.glsl_version                             = 460;
    m_info.api_info                                 = fmt::format("Metal ({})", m_mtl_device->name()->utf8String());
    m_info.max_texture_size                         = 16384;
    m_info.max_samples                              = max_samples;
    m_info.max_color_texture_samples                = max_samples;
    m_info.max_depth_texture_samples                = max_samples;
    m_info.max_framebuffer_samples                  = max_samples;
    m_info.max_integer_samples                      = max_samples;
    m_info.max_vertex_attribs                       = 31;
    m_info.use_persistent_buffers                   = true;
    m_info.use_compute_shader                       = true;
    m_info.use_shader_storage_buffers               = true;
    m_info.use_multi_draw_indirect_core             = false;
    m_info.uniform_buffer_offset_alignment          = 256;
    m_info.shader_storage_buffer_offset_alignment   = 256;
    m_info.max_uniform_buffer_bindings              = 31;
    m_info.max_shader_storage_buffer_bindings       = 31;
    m_info.max_combined_texture_image_units         = 128;
    m_info.max_3d_texture_size                      = 2048;
    m_info.max_cube_map_texture_size                = 16384;
    m_info.max_texture_buffer_size                  = static_cast<int>(m_mtl_device->maxBufferLength());
    m_info.max_array_texture_layers                 = 2048;
    m_info.max_uniform_block_size                   = 65536;
    m_info.use_texture_view                         = true;
    m_info.max_per_stage_descriptor_samplers        = 256; // Tier 2 argument buffers remove per-stage limit
    m_info.texture_heap_path                       = Texture_heap_path::metal_argument_buffer;
    m_info.max_compute_workgroup_count[0]           = 65535;
    m_info.max_compute_workgroup_count[1]           = 65535;
    m_info.max_compute_workgroup_count[2]           = 65535;
    m_info.max_compute_workgroup_size[0]            = static_cast<int>(max_threads.width);
    m_info.max_compute_workgroup_size[1]            = static_cast<int>(max_threads.height);
    m_info.max_compute_workgroup_size[2]            = static_cast<int>(max_threads.depth);
    m_info.max_compute_work_group_invocations       = static_cast<int>(max_threads.width); // conservative
    m_info.max_compute_shared_memory_size           = static_cast<int>(m_mtl_device->maxThreadgroupMemoryLength());
    m_info.max_vertex_shader_storage_blocks         = 31;
    m_info.max_vertex_uniform_blocks                = 31;
    m_info.max_vertex_uniform_vectors               = 4096;
    m_info.max_fragment_shader_storage_blocks       = 31;
    m_info.max_fragment_uniform_blocks              = 31;
    m_info.max_fragment_uniform_vectors              = 4096;
    m_info.max_geometry_shader_storage_blocks       = 0;
    m_info.max_geometry_uniform_blocks              = 0;
    m_info.max_tess_control_shader_storage_blocks   = 0;
    m_info.max_tess_control_uniform_blocks          = 0;
    m_info.max_tess_evaluation_shader_storage_blocks = 0;
    m_info.max_tess_evaluation_uniform_blocks       = 0;
    m_info.max_compute_shader_storage_blocks        = 31;
    m_info.max_compute_uniform_blocks               = 31;
    m_info.max_texture_max_anisotropy               = 16.0f;
    m_info.max_depth_layers                         = 4;
    m_info.max_depth_resolution                     = 4096;

    // Metal MSAA depth/stencil resolve filter support is fixed by OS version.
    // erhe targets macOS 10.15 / iOS 13 or later, where:
    //   - MTLMultisampleDepthResolveFilter   has sample0 / min / max (no average).
    //   - MTLMultisampleStencilResolveFilter has sample0 / depthResolvedSample.
    // We expose only sample_zero / min / max for depth and sample_zero for stencil
    // through the cross-API enum (depthResolvedSample is intentionally not exposed
    // because it has no Vulkan analogue).
    m_info.supported_depth_resolve_modes =
        Resolve_mode_flag_bit_mask::sample_zero |
        Resolve_mode_flag_bit_mask::min |
        Resolve_mode_flag_bit_mask::max;
    m_info.supported_stencil_resolve_modes = Resolve_mode_flag_bit_mask::sample_zero;
    // Metal allows depth and stencil filters to be set independently.
    m_info.independent_depth_stencil_resolve      = true;
    m_info.independent_depth_stencil_resolve_none = true;

    // Metal coordinate conventions
    // Metal viewport transform uses y_w = (1 - y_ndc)/2 * h + originY, which
    // maps positive NDC Y to the top of the screen. No projection Y-flip needed.
    m_info.coordinate_conventions.native_depth_range = erhe::math::Depth_range::zero_to_one;
    m_info.coordinate_conventions.framebuffer_origin = erhe::math::Framebuffer_origin::top_left;
    m_info.coordinate_conventions.texture_origin     = erhe::math::Texture_origin::top_left;
    m_info.coordinate_conventions.clip_space_y_flip  = erhe::math::Clip_space_y_flip::disabled;

    // Enable debug groups for Metal GPU capture
    Scoped_debug_group_impl::s_enabled = true;

    if (surface_create_info.context_window != nullptr) {
        m_surface = std::make_unique<Surface>(
            std::make_unique<Surface_impl>(*this, surface_create_info)
        );
    }
}

Device_impl::~Device_impl() noexcept
{
    // Resource destructors enqueue completion handlers, so the surface and
    // ring buffers must be torn down before we drain the queue. Surface
    // destruction in turn enqueues handlers (e.g. for the swapchain texture
    // argument encoder), so we need to drain *after* this point.
    m_surface.reset();
    m_ring_buffers.clear();
    if (m_texture_argument_encoder != nullptr) {
        m_texture_argument_encoder->release();
        m_texture_argument_encoder = nullptr;
    }

    // Wait for the GPU to finish anything still in flight, then drain every
    // queued completion handler unconditionally. Mirrors the Vulkan
    // ~Device_impl() teardown order.
    wait_idle();
    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback(*this);
    }
    m_completion_handlers.clear();

    if (m_mtl_log_state != nullptr) {
        m_mtl_log_state->release();
        m_mtl_log_state = nullptr;
    }
    if (m_default_sampler_state != nullptr) {
        m_default_sampler_state->release();
        m_default_sampler_state = nullptr;
    }
    if (m_mtl_command_queue != nullptr) {
        m_mtl_command_queue->release();
        m_mtl_command_queue = nullptr;
    }
    if (m_mtl_device != nullptr) {
        m_mtl_device->release();
        m_mtl_device = nullptr;
    }
}

auto Device_impl::wait_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::idle);
    m_state = Device_frame_state::waited;
    return true;
}

auto Device_impl::begin_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::waited);

    // Drain frames whose cbs have signalled GPU completion via
    // Command_buffer_impl's addCompletedHandler. Ring buffer ranges
    // pinned to those frames become safe to recycle; per-frame
    // completion handlers run now.
    std::vector<uint64_t> drained;
    {
        std::lock_guard<std::mutex> lock{m_completion_mutex};
        drained.swap(m_pending_completed_frames);
    }
    for (const uint64_t completed_frame : drained) {
        for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
            ring_buffer->frame_completed(completed_frame);
        }
        frame_completed(completed_frame);
    }

    m_state = Device_frame_state::recording;
    return true;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    // Legacy compat: swapchain part moved to Command_buffer::begin_swapchain.
    static_cast<void>(frame_begin_info);
    return begin_frame();
}

auto Device_impl::end_frame() -> bool
{
    // CONTRACT: end_frame advances the frame index. That is its ONLY
    // job. It does not submit, it does not present. See
    // erhe_graphics/notes.md ("Frame lifecycle") and the Vulkan
    // implementation for the rationale.
    ERHE_VERIFY(
        (m_state == Device_frame_state::in_swapchain_frame) ||
        (m_state == Device_frame_state::recording) ||
        (m_state == Device_frame_state::waited)
    );

    ++m_frame_index;
    m_state = Device_frame_state::idle;
    return true;
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    // Legacy compat overload; Frame_end_info is no longer used.
    static_cast<void>(frame_end_info);
    return end_frame();
}


auto Device_impl::is_in_swapchain_frame() const -> bool
{
    return m_state == Device_frame_state::in_swapchain_frame;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

auto Device_impl::get_native_handles() const -> Native_device_handles
{
    return Native_device_handles{};
}

void Device_impl::resize_swapchain_to_window()
{
    // TODO Implement Metal swapchain resize
}

auto Device_impl::get_command_buffer(const unsigned int thread_slot) -> Command_buffer&
{
    ERHE_VERIFY(thread_slot < s_number_of_thread_slots);

    std::lock_guard<std::mutex> lock{m_per_thread_mutex};
    Per_thread_command_buffers::Allocated_command_buffer entry{};
    entry.frame_index    = m_frame_index;
    entry.command_buffer = std::make_unique<Command_buffer>(m_device);

    // Hand the cb its implicit GPU + CPU sync primitives. Allocation is
    // cheap on Metal so we go straight through MTL::Device rather than
    // through a pool. Released by ~Command_buffer_impl.
    if (m_mtl_device != nullptr) {
        MTL::Event*       gpu_event = m_mtl_device->newEvent();
        MTL::SharedEvent* cpu_event = m_mtl_device->newSharedEvent();
        entry.command_buffer->get_impl().set_implicit_sync(gpu_event, cpu_event);
    }

    Command_buffer& reference = *entry.command_buffer;
    m_per_thread_command_buffers[thread_slot].allocated.push_back(std::move(entry));
    return reference;
}

void Device_impl::submit_command_buffers(std::span<Command_buffer* const> command_buffers)
{
    if (command_buffers.empty()) {
        return;
    }

    // CPU-side wait phase: any wait_for_cpu(other) registered on the
    // cbs in this batch translates into MTL::SharedEvent::waitUntilSignaledValue
    // before the cb is committed (Step 7).
    for (Command_buffer* const command_buffer : command_buffers) {
        ERHE_VERIFY(command_buffer != nullptr);
        command_buffer->get_impl().pre_submit_wait();
    }

    // Encode GPU sync events and commit each cb in submit order. Metal
    // submits FIFO on a single queue, so order across the span is
    // preserved on the GPU.
    for (Command_buffer* const command_buffer : command_buffers) {
        Command_buffer_impl& impl = command_buffer->get_impl();
        impl.encode_synchronization();

        MTL::CommandBuffer* mtl_cb = impl.get_mtl_command_buffer();
        if (mtl_cb == nullptr) {
            // begin() failed (queue allocation refused); nothing to commit.
            impl.clear_swapchain_used();
            continue;
        }

        // When this cb engaged a swapchain via begin_swapchain, encode
        // presentDrawable on it before commit. Mirrors Vulkan's
        // swapchains_to_present handling at the end of submit.
        Swapchain_impl* swapchain = impl.get_swapchain_used();
        if (swapchain != nullptr) {
            CA::MetalDrawable* drawable = swapchain->get_current_drawable();
            if (drawable != nullptr) {
                mtl_cb->presentDrawable(drawable);
            }
        }
        impl.clear_swapchain_used();

        mtl_cb->commit();
    }
}

void Device_impl::add_completion_handler(std::function<void(Device_impl&)> callback)
{
    m_completion_handlers.emplace_back(m_frame_index, std::move(callback));
}

void Device_impl::frame_completed(const uint64_t completed_frame)
{
    for (const Completion_handler& entry : m_completion_handlers) {
        if (entry.frame_number == completed_frame) {
            entry.callback(*this);
        }
    }
    auto i = std::remove_if(
        m_completion_handlers.begin(),
        m_completion_handlers.end(),
        [completed_frame](const Completion_handler& entry) { return entry.frame_number == completed_frame; }
    );
    if (i != m_completion_handlers.end()) {
        m_completion_handlers.erase(i, m_completion_handlers.end());
    }

    // Drop Command_buffers whose frame has retired on the GPU. Releasing
    // our std::unique_ptr only drops our retain on MTL::CommandBuffer;
    // Metal's queue keeps the cb alive until completion handlers run, so
    // dropping here is safe even if some cbs from the same frame have
    // not completed yet (their MTL retain count keeps them in flight).
    {
        std::lock_guard<std::mutex> lock{m_per_thread_mutex};
        for (Per_thread_command_buffers& slot : m_per_thread_command_buffers) {
            std::erase_if(
                slot.allocated,
                [completed_frame](const Per_thread_command_buffers::Allocated_command_buffer& entry) {
                    return entry.frame_index <= completed_frame;
                }
            );
        }
    }
}

void Device_impl::notify_command_buffer_completed(const uint64_t frame_index)
{
    std::lock_guard<std::mutex> lock{m_completion_mutex};
    m_pending_completed_frames.push_back(frame_index);
}

auto Device_impl::recreate_surface_for_new_window() -> bool
{
    // Not applicable on Metal -- CAMetalLayer is recreated by the
    // platform layer; erhe's Surface wrapper is one-shot. Vulkan-only.
    return false;
}

void Device_impl::wait_idle()
{
    // Metal has no vkDeviceWaitIdle equivalent. The standard idiom is to
    // submit an empty command buffer on the queue and wait for it to
    // complete -- because command buffers from the same queue are
    // FIFO-ordered, this guarantees all earlier work has finished.
    if (m_mtl_command_queue == nullptr) {
        return;
    }
    MTL::CommandBuffer* command_buffer = m_mtl_command_queue->commandBuffer();
    if (command_buffer == nullptr) {
        return;
    }
    command_buffer->commit();
    command_buffer->waitUntilCompleted();

    // All prior cbs (including device-frame cbs) have now completed on
    // the GPU. Drain any pending frame-completion enqueues and flush
    // their frame_completed callbacks so staging buffers / ring buffer
    // ranges are released before wait_idle returns (used by the editor
    // ctor's init-frame flow).
    std::vector<uint64_t> drained;
    {
        std::lock_guard<std::mutex> lock{m_completion_mutex};
        drained.swap(m_pending_completed_frames);
    }
    for (const uint64_t completed_frame : drained) {
        for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
            ring_buffer->frame_completed(completed_frame);
        }
        frame_completed(completed_frame);
    }
}

void Device_impl::on_thread_enter()
{
    // TODO Create NS::AutoreleasePool for this thread
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0; // Metal uses argument buffers instead of bindless handles
}

auto Device_impl::create_dummy_texture(Command_buffer& /*init_command_buffer*/, const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
{
    const Texture_create_info create_info{
        .device       = m_device,
        .usage_mask   = Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::transfer_dst,
        .type         = Texture_type::texture_2d,
        .pixelformat  = format,
        .use_mipmaps  = false,
        .sample_count = 0,
        .width        = 2,
        .height       = 2,
        .debug_label  = erhe::utility::Debug_label{"metal dummy texture"}
    };
    return std::make_shared<Texture>(m_device, create_info);
}

auto Device_impl::get_buffer_alignment(const Buffer_target target) -> std::size_t
{
    switch (target) {
        case Buffer_target::storage: {
            return m_info.shader_storage_buffer_offset_alignment;
        }
        case Buffer_target::uniform: {
            return m_info.uniform_buffer_offset_alignment;
        }
        case Buffer_target::draw_indirect: {
            return sizeof(Draw_indexed_primitives_indirect_command);
        }
        default: {
            return 64;
        }
    }
}

auto Device_impl::get_frame_index() const -> uint64_t
{
    return m_frame_index;
}

auto Device_impl::allocate_ring_buffer_entry(
    const Buffer_target     buffer_target,
    const Ring_buffer_usage  usage,
    const std::size_t       byte_count
) -> Ring_buffer_range
{
    const std::size_t required_alignment = erhe::utility::next_power_of_two_16bit(get_buffer_alignment(buffer_target));
    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap{0};

    // Pass 1: find buffer usable without wrap
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_without_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, usage, byte_count);
        }
    }

    // Pass 2: find buffer usable with wrap
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_with_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, usage, byte_count);
        }
    }

    // No existing buffer found, create new one
    const Ring_buffer_create_info create_info{
        .size              = std::max(m_min_buffer_size, 4 * byte_count),
        .ring_buffer_usage = usage,
        .debug_label       = "Ring_buffer"
    };
    m_ring_buffers.push_back(std::make_unique<Ring_buffer>(m_device, create_info));
    return m_ring_buffers.back()->acquire(required_alignment, usage, byte_count);
}

void Device_impl::start_frame_capture() {}
void Device_impl::end_frame_capture() {}

auto Device_impl::make_blit_command_encoder(Command_buffer& command_buffer) -> Blit_command_encoder
{
    return Blit_command_encoder{m_device, command_buffer};
}

auto Device_impl::make_compute_command_encoder(Command_buffer& command_buffer) -> Compute_command_encoder
{
    return Compute_command_encoder{m_device, command_buffer};
}

auto Device_impl::make_render_command_encoder(Command_buffer& command_buffer) -> Render_command_encoder
{
    return Render_command_encoder{m_device, command_buffer};
}

auto Device_impl::get_format_properties(const erhe::dataformat::Format format) const -> Format_properties
{
    static_cast<void>(format);
    Format_properties properties{};
    properties.supported                    = true;
    properties.texture_2d_sample_counts.push_back(1);
    for (int count : {2, 4, 8}) {
        if (m_mtl_device->supportsTextureSampleCount(static_cast<NS::UInteger>(count))) {
            properties.texture_2d_sample_counts.push_back(count);
        }
    }
    properties.texture_2d_array_max_width   = 16384;
    properties.texture_2d_array_max_height  = 16384;
    properties.texture_2d_array_max_layers  = 2048;
    return properties;
}

auto Device_impl::probe_image_format_support(const erhe::dataformat::Format format, const uint64_t usage_mask) const -> bool
{
    // Metal has no direct equivalent of vkGetPhysicalDeviceImageFormatProperties2.
    // The probe concept is Vulkan-specific; on Metal we conservatively report
    // "supported" and let texture creation fail if the combination is not valid.
    static_cast<void>(format);
    static_cast<void>(usage_mask);
    return true;
}

auto Device_impl::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    return std::vector<erhe::dataformat::Format>{
        erhe::dataformat::Format::format_d16_unorm,
        erhe::dataformat::Format::format_d32_sfloat,
        erhe::dataformat::Format::format_d32_sfloat_s8_uint
    };
}

void Device_impl::sort_depth_stencil_formats(
    std::vector<erhe::dataformat::Format>& formats,
    const unsigned int                     sort_flags,
    const int                              requested_sample_count
) const
{
    static_cast<void>(formats);
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
    // TODO Implement Metal format sorting
}

auto Device_impl::choose_depth_stencil_format(const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format
{
    if (!formats.empty()) {
        return formats.front();
    }
    return erhe::dataformat::Format::format_d32_sfloat;
}

auto Device_impl::choose_depth_stencil_format(const unsigned int sort_flags, const int requested_sample_count) const -> erhe::dataformat::Format
{
    static_cast<void>(requested_sample_count);
    // Honor format_flag_require_stencil so callers asking for a stencil-
    // bearing depth format get one. d32_sfloat_s8_uint is the only stencil-
    // bearing depth format Metal exposes via MTLPixelFormatDepth32Float_Stencil8;
    // d24_unorm_s8_uint is not available on Apple silicon.
    if ((sort_flags & format_flag_require_stencil) != 0) {
        return erhe::dataformat::Format::format_d32_sfloat_s8_uint;
    }
    return erhe::dataformat::Format::format_d32_sfloat;
}

auto Device_impl::get_shader_monitor() -> Shader_monitor&
{
    return m_shader_monitor;
}

auto Device_impl::get_info() const -> const Device_info&
{
    return m_info;
}

auto Device_impl::get_graphics_config() const -> const Graphics_config&
{
    return m_graphics_config;
}

void Device_impl::reset_shader_stages_state_tracker()
{
    // TODO Implement if needed for Metal
}

auto Device_impl::get_draw_id_uniform_location() const -> GLint
{
    return -1;
}

auto Device_impl::get_mtl_device() const -> MTL::Device*
{
    return m_mtl_device;
}

auto Device_impl::get_mtl_command_queue() const -> MTL::CommandQueue*
{
    return m_mtl_command_queue;
}

auto Device_impl::get_default_sampler_state() const -> MTL::SamplerState*
{
    return m_default_sampler_state;
}

auto Device_impl::get_texture_argument_encoder() const -> MTL::ArgumentEncoder*
{
    return m_texture_argument_encoder;
}

void Device_impl::set_texture_argument_encoder(MTL::ArgumentEncoder* encoder)
{
    if (m_texture_argument_encoder != nullptr) {
        m_texture_argument_encoder->release();
    }
    m_texture_argument_encoder = encoder;
}

auto Device_impl::get_texture_arg_buffer_layout() const -> const Texture_arg_buffer_layout&
{
    return m_texture_arg_buffer_layout;
}

void Device_impl::set_texture_arg_buffer_layout(const Texture_arg_buffer_layout& layout)
{
    m_texture_arg_buffer_layout = layout;
}

} // namespace erhe::graphics
