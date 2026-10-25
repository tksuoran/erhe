#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#include "erhe_graphics/clock_monitor_apple.hpp"
#include "erhe_graphics/metal4/metal4_gpu_prewarm.hpp"
#include "erhe_graphics/metal4/metal4_gpu_timer.hpp"
#include "erhe_graphics/metal4/metal4_indirect_count_draws.hpp"
#include "erhe_graphics/metal4/metal4_pipeline_compiler.hpp"
#include "erhe_graphics/metal4/metal4_pipeline_state_cache.hpp"
#include "erhe_graphics/metal4/metal4_scoped_debug_group.hpp"
#include "erhe_graphics/metal4/metal4_helpers.hpp"
#include "erhe_graphics/metal4/metal4_surface.hpp"
#include "erhe_graphics/metal4/metal4_texture.hpp"
#include "erhe_graphics/metal4/metal4_pixel_format_table.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/display_refresh_resolver.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/metal4/metal4_swapchain.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"
#include "erhe_window/window.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "erhe_utility/align.hpp"

#include <fmt/format.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <objc/runtime.h>

namespace erhe::graphics {

namespace {

// The device frame whose end logs the pipeline startup summary
// (Pipeline_compiler::log_startup_summary): late enough for the pipelines
// the editor creates in its first main loop frames (its loading screen
// renders device frames before them), early enough that the startup window,
// in which every pipeline creation probes the archive, stays short.
constexpr uint64_t c_pipeline_startup_summary_frame = 300;

} // anonymous namespace

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config)
    : m_device         {device}
    , m_shader_monitor {device}
    , m_graphics_config{graphics_config}
{

    install_metal_error_handler(device);

    // Must happen before the first Metal framework use: GPUToolsCapture loads
    // when Metal initializes with METAL_CAPTURE_ENABLED set (Xcode's scheme
    // sets it whenever GPU Frame Capture is enabled, even when not capturing),
    // and once loaded it cannot be unloaded.
    if (graphics_config.metal.disable_gpu_frame_capture) {
        if (objc_getClass("CaptureMTLCommandBuffer") != nullptr) {
            log_startup->warn(
                "Metal: metal.disable_gpu_frame_capture is set but the GPU frame-capture layer "
                "is already loaded - too late to suppress it (ray query stays disabled under it)"
            );
        } else {
            unsetenv("METAL_CAPTURE_ENABLED");
            log_startup->info(
                "Metal: GPU frame-capture layer suppressed (metal.disable_gpu_frame_capture); "
                "Xcode GPU frame capture will NOT work for this run"
            );
        }
    }

    m_mtl_device = MTL::CreateSystemDefaultDevice();
    m_gpu_timer_sample_pool = std::make_unique<Gpu_timer_sample_pool>(m_mtl_device);
    if (m_mtl_device == nullptr) {
        log_startup->error("Failed to create Metal device");
        return;
    }

    log_startup->info("Metal 4 backend, device: {}", m_mtl_device->name()->utf8String());

    // Require Metal 4 (GPU family Metal4): the backend submits through the
    // MTL4 command queue, argument tables and residency sets.
    if (!m_mtl_device->supportsFamily(MTL::GPUFamilyMetal4)) {
        log_startup->critical(
            "Metal device '{}' does not support GPUFamilyMetal4 (Metal 4). "
            "The Metal 4 backend requires it; use the Metal 3 backend (ERHE_GRAPHICS_API=metal).",
            m_mtl_device->name()->utf8String()
        );
        abort();
    }

    // Resolve the device's GPU family once; the pixel-format capability table
    // (metal_pixel_format_table, a translation of Metal-Feature-Set-Tables.pdf)
    // is indexed by it in get_format_properties().
    m_gpu_family = metal::metal_resolve_gpu_family(m_mtl_device);
    log_startup->info("Metal GPU family: {}", metal::metal_gpu_family_name(m_gpu_family));

    // Runtime cap the static feature-set table cannot express: the tables list
    // 32-bit float color formats as non-filterable, with a footnote that linear
    // filtering is gated by supports32BitFloatFiltering (true on all Apple
    // silicon). Query it once and apply it in get_format_properties(), mirroring
    // MoltenVK's modifyMTLFormatCapabilities runtime adjustment.
    m_supports_32bit_float_filtering = m_mtl_device->supports32BitFloatFiltering();

    m_mtl_command_queue = m_mtl_device->newMTL4CommandQueue();
    if (m_mtl_command_queue == nullptr) {
        log_startup->critical("Metal 4: newMTL4CommandQueue failed");
        abort();
    }
    // Every library and pipeline state goes through the compiler, which
    // loads the pipeline archive of this GPU and OS build.
    m_pipeline_compiler    = std::make_unique<Pipeline_compiler>(m_mtl_device);
    m_pipeline_state_cache = std::make_unique<Pipeline_state_cache>(*this);
    m_residency        = std::make_unique<Residency>(m_mtl_device);
    m_submission_pools = std::make_unique<Submission_pools>(m_mtl_device, *m_residency);
    m_mtl_command_queue->addResidencySet(m_residency->get_residency_set());
    m_in_flight_serials.reserve(256);

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
    m_info.api_info                                 = fmt::format("Metal 4 ({})", m_mtl_device->name()->utf8String());
    m_info.max_texture_size                         = 16384;
    m_info.sub_pixel_precision_bits                 = 0; // Metal does not report it
    // Each multi-draw command is an indirect drawIndexedPrimitives that
    // reads its base instance from the command, and MSL's [[instance_id]]
    // (SPIRV-Cross's gl_InstanceIndex) includes the base instance: records
    // are named by first_instance, as on Vulkan.
    m_info.record_indexing                          = Record_indexing::first_instance;
    // Base vertex and base instance in (indirect) draws: every Metal 3 GPU
    // (Apple3+ and Mac2 families) supports them.
    m_info.use_base_instance                        = true;
    m_info.max_samples                              = max_samples;
    m_info.max_color_texture_samples                = max_samples;
    m_info.max_depth_texture_samples                = max_samples;
    m_info.max_framebuffer_samples                  = max_samples;
    m_info.max_integer_samples                      = max_samples;
    m_info.max_vertex_attribs                       = 31;
    m_info.use_persistent_buffers                   = true;
    // GPU ray tracing (Apple7+ / Apple silicon): ray query GLSL lowers to
    // MSL intersection_query via SPIRV-Cross, acceleration structures build
    // through MTL::AccelerationStructureCommandEncoder. Position fetch has
    // no MSL lowering (SPIRV-Cross emits the GLSL intrinsic verbatim), so
    // use_ray_tracing_position_fetch stays false and ray query consumers
    // take their buffer-fetch fallback path.
    //
    // Xcode's GPU frame-capture layer (GPUToolsCapture, loaded when the
    // scheme's GPU Frame Capture is enabled or METAL_CAPTURE_ENABLED=1)
    // crashes on EVERY acceleration structure command encoder: its
    // endEncoding encodes a signal event that throws
    // NSInvalidArgumentException inside the AGX driver (reproduced with a
    // minimal standalone build on macOS 26 / M4). Degrade to no ray query
    // under it instead of dying the moment a bake or ray trace runs; set
    // the scheme's GPU Frame Capture to Disabled to debug with ray tracing.
    const bool gpu_capture_layer = objc_getClass("CaptureMTLCommandBuffer") != nullptr;
    if (m_mtl_device->supportsRaytracing() && gpu_capture_layer) {
        log_startup->warn(
            "Metal: Xcode GPU frame-capture layer detected - disabling GPU ray tracing "
            "(acceleration structure encoders crash inside GPUToolsCapture; "
            "set the scheme's GPU Frame Capture to Disabled to use ray tracing under the debugger)"
        );
    }
    m_info.use_ray_query                            = m_mtl_device->supportsRaytracing() && !gpu_capture_layer;

    // Acceleration structure vertex stride rules, from
    // MTLAccelerationStructureTriangleGeometryDescriptor::vertexStride: the stride
    // must be a multiple of the vertex format size, and at least 12 bytes. A
    // quantized stream 0 is padded to 8 (unskinned) or 16 (skinned) bytes, so
    // MTL::AttributeFormatShort3Normalized (6 bytes) never divides it, while
    // Short4Normalized (8 bytes) divides both. The 12 byte minimum still rules the
    // unskinned stride out, which is why the minimum is published rather than
    // silently violated - Mesh_memory declines quantization when this device also
    // ray traces. See doc/erhe/vertex_position_quantization.md.
    m_info.use_16_vec3_snorm_acceleration_structure_vertex_buffer = false;
    m_info.use_16_vec4_snorm_acceleration_structure_vertex_buffer = true;
    m_info.min_acceleration_structure_vertex_stride = 12;
    m_info.ray_query_disabled_by_capture_layer      = m_mtl_device->supportsRaytracing() && gpu_capture_layer;
    m_info.use_multi_draw_indirect_core             = false;
    // The GPU-read draw count: prepared count draws (Indirect_count_draws),
    // whose encoding kernel is raw MSL compiled here.
    m_indirect_count_encode_pipeline = make_indirect_count_encode_pipeline(*m_pipeline_compiler);
    m_info.use_draw_indirect_count                  = (m_indirect_count_encode_pipeline != nullptr);
    log_startup->info(
        "Draw indirect count: {} (indirect command buffer encoded by a kernel, Indirect_count_draws)",
        m_info.use_draw_indirect_count
    );
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
    m_info.texture_heap_size = max_texture_heap_size;
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
    // Fragment functions write and atomically modify device buffers.
    m_info.fragment_stores_and_atomics              = true;
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

    init_frame_pacing(surface_create_info.context_window);
}

void Device_impl::init_frame_pacing(erhe::window::Context_window* const context_window)
{
    Display_refresh_resolver& resolver = m_device.get_display_refresh_resolver();

    // The window-side display refresh source: the display the window is on,
    // now and whenever the window moves or that display changes mode
    // (display_refresh_resolver.hpp). The only source on Metal.
    if (context_window != nullptr) {
        Device* const owner_device = &m_device;
        Device_impl* const device_impl = this;
        const auto feed_window_period = [context_window, owner_device, device_impl]() {
            const std::string display_name = context_window->get_display_name();
            owner_device->get_display_refresh_resolver().set_window_period(
                context_window->get_display_refresh_period_seconds(),
                display_name.c_str(),
                context_window->enumerate_displays().size()
            );
            // The display's presentation (claim C30): a variable refresh rate
            // display driven fullscreen has no grid for the pacer to track.
            // Re-read with the period: on a display change, a mode change and
            // entering or leaving fullscreen.
            // The present calibration's cache key follows the display, its
            // mode and the fullscreen state (doc/erhe/graphics.md "Present
            // calibration cache"): a changed key re-seeds the pacer from the
            // cached entry, or clears the calibration.
            const bool calibration_changed = owner_device->refresh_present_calibration_for_display();
            bool presentation_applied = false;
            if ((device_impl->m_surface != nullptr) && !device_impl->m_surface->get_impl().is_headless()) {
                presentation_applied = owner_device->get_display_refresh_resolver().set_window_presentation(
                    device_impl->m_surface->get_impl().query_display_presentation()
                );
            }
            if (calibration_changed && !presentation_applied) {
                owner_device->get_display_refresh_resolver().notify_presentation();
            }
        };
        feed_window_period();
        context_window->register_display_changed_callback(feed_window_period);
        m_frame_pacing_window = context_window;
    }
    // The init calibration is a Vulkan swapchain feature; without it the
    // resolver must not stay in its calibrating state, which holds the pacer
    // actuators idle (Device::is_display_refresh_calibrating).
    resolver.skip_calibration("the Metal backend has none");

    // Tier resolution (doc/frame_pacing/capability_tiers.md section 2). Both
    // tiers need a display-synced CAMetalLayer (Surface_impl::apply_frame_pacing_tier).
    // Tier S senses its backpressure: nextDrawable blocks once the
    // compositor holds the drawable pool. Tier M is tier W on Metal: the
    // drawable's presented handler gives presentation feedback and present
    // wait (Present_feedback), the drawable's presentAtTime: the target present
    // time, which macOS honors (measured, doc/erhe/metal_backend.md "Frame
    // pacing"), so it needs no C15 present-request holdback.
    const bool         has_layer = (m_surface != nullptr) && !m_surface->get_impl().is_headless();
    if (has_layer) {
        m_present_feedback = std::make_shared<Present_feedback>(m_device.get_frame_time_recorder());
    }
    const std::string& requested = get_graphics_config().frame_pacing_tier;
    if (requested == "off") {
        m_frame_pacing_tier = Frame_pacing_tier::off;
    } else if (!has_layer) {
        m_frame_pacing_tier = Frame_pacing_tier::off;
        log_context->info("frame pacing: no CAMetalLayer (headless), so no presentation backpressure; tier OFF");
    } else if (get_graphics_config().force_disable_vsync) {
        m_frame_pacing_tier = Frame_pacing_tier::off;
        log_context->info("frame pacing: force_disable_vsync leaves no presentation backpressure; tier OFF");
    } else if (requested == "s") {
        m_frame_pacing_tier = Frame_pacing_tier::slop_servo;
    } else {
        // auto: tier M (live gate in doc/plans/frame_pacing_metal.md).
        if ((requested != "w") && (requested != "auto")) {
            log_context->warn("frame_pacing_tier '{}' not recognized; using 'auto'", requested);
        }
        m_frame_pacing_tier = Frame_pacing_tier::full;
    }
    if (m_surface != nullptr) {
        m_surface->get_impl().apply_frame_pacing_tier(m_frame_pacing_tier);
    }
    if (m_frame_pacing_tier == Frame_pacing_tier::full) {
        // The clock monitor and the pre-warm kernel first: the present
        // calibration measures the pre-warm with them.
        m_clock_monitor = std::make_shared<Clock_monitor>(m_device.get_frame_time_recorder());
        if (!m_clock_monitor->is_available()) {
            m_clock_monitor.reset();
        }
        m_gpu_prewarm = std::make_unique<Gpu_prewarm>(*this);
        if (!m_gpu_prewarm->is_available()) {
            m_gpu_prewarm.reset();
        }
        run_present_calibration();
    }
    log_context->info(
        "frame pacing tier = {}",
        (m_frame_pacing_tier == Frame_pacing_tier::full)       ? "M (full pacing: presented handler feedback, presentAtTime:)" :
        (m_frame_pacing_tier == Frame_pacing_tier::slop_servo) ? "S (slop-servo fallback)" : "OFF"
    );
}

Device_impl::~Device_impl() noexcept
{
    // The display changed callback reaches this device and its surface.
    if (m_frame_pacing_window != nullptr) {
        m_frame_pacing_window->register_display_changed_callback({});
    }
    if (m_present_feedback != nullptr) {
        m_present_feedback->detach();
    }
    if (m_clock_monitor != nullptr) {
        m_clock_monitor->detach();
    }
    // Resource destructors enqueue completion handlers, so the surface and
    // ring buffers must be torn down before we drain the queue. Surface
    // destruction in turn enqueues handlers (e.g. for the swapchain texture
    // argument encoder), so we need to drain *after* this point.
    // MTL4 command buffers retain nothing: wait until the GPU is done with
    // everything committed before the surface (swapchain textures, capture
    // buffer) and the pipelines below are released at once.
    wait_idle();
    // Precondition: every thread that creates pipelines (the editor's
    // worker threads, the render thread) has stopped. The archive then
    // holds every pipeline this run created. Pipeline states do not depend
    // on the compiler, so the ones released below and by the drained
    // completion handlers outlive it safely.
    m_pipeline_state_cache.reset();
    if (m_pipeline_compiler != nullptr) {
        m_pipeline_compiler->log_startup_summary();
        m_pipeline_compiler->write_archive();
        m_pipeline_compiler.reset();
    }
    m_surface.reset();
    m_ring_buffer_pool.clear();
    for (Blit_framebuffer_pipeline& entry : m_blit_framebuffer_pipelines) {
        entry.pipeline->release();
    }
    m_blit_framebuffer_pipelines.clear();
    if (m_blit_framebuffer_library != nullptr) {
        m_blit_framebuffer_library->release();
        m_blit_framebuffer_library = nullptr;
    }
    if (m_indirect_count_encode_pipeline != nullptr) {
        m_indirect_count_encode_pipeline->release();
        m_indirect_count_encode_pipeline = nullptr;
    }
    if (m_texture_argument_encoder != nullptr) {
        m_texture_argument_encoder->release();
        m_texture_argument_encoder = nullptr;
    }

    // Wait for the GPU to finish anything still in flight, then drain every
    // queued completion handler unconditionally. Mirrors the Vulkan
    // ~Device_impl() teardown order.
    wait_idle();
    {
        std::lock_guard<std::mutex> lock{m_completion_handler_mutex};
        m_completion_handler_scratch.swap(m_completion_handlers);
    }
    for (const Completion_handler& entry : m_completion_handler_scratch) {
        entry.callback(*this);
    }
    m_completion_handler_scratch.clear();

    // The command buffers still held return their recordings to the pools,
    // which go before the residency set and the queue.
    {
        std::lock_guard<std::mutex> lock{m_per_thread_mutex};
        for (Per_thread_command_buffers& slot : m_per_thread_command_buffers) {
            slot.allocated.clear();
        }
    }
    m_gpu_prewarm.reset();
    m_submission_pools.reset();
    if ((m_mtl_command_queue != nullptr) && (m_residency != nullptr)) {
        m_mtl_command_queue->removeResidencySet(m_residency->get_residency_set());
    }
    m_residency.reset();

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

void Device_impl::wait_for_frame_start(const uint64_t frame_id)
{
    // No GPU clock calibration or frame bracket on Metal yet; the frame
    // time record is opened by Device::wait_for_frame_start.
    static_cast<void>(frame_id);
}

auto Device_impl::get_frame_slot_index(const uint64_t frame_id) const -> std::size_t
{
    return static_cast<std::size_t>(frame_id % static_cast<uint64_t>(s_max_frames_in_flight));
}

auto Device_impl::acquire_frame_slot(const uint64_t frame_id) -> bool
{
    // Order rule (doc/erhe/graphics.md "Frame lifecycle"): frames are
    // acquired strictly in id order, at most N open.
    erhe::utility::Thread_roles::assert_main_thread();

    // Pacing: the frame that last used this slot, frame_id - N, has ended
    // (at most N frames are open, verified below), so every command buffer
    // of it is committed; wait until the GPU has completed them all, as the
    // Vulkan timeline wait does. Without it nothing bounds how far the CPU
    // runs ahead of a GPU-bound frame when no drawable is presented
    // (headless), and the ring buffer pool spills instead.
    // The wait is the involuntary fence wait of the frame time record (the
    // vkWaitForFences analogue; doc/frame_pacing/inputs.md 3.2).
    const uint64_t frames_in_flight = static_cast<uint64_t>(s_max_frames_in_flight);
    if (frame_id > frames_in_flight) {
        const double fence_wait_begin = erhe::frame_pacing::Frame_time_recorder::now();
        wait_for_frame_gpu_completion(frame_id - frames_in_flight);
        m_device.get_frame_time_recorder().add(
            static_cast<std::int64_t>(frame_id),
            &erhe::frame_pacing::Frame_time_record::fence_wait_duration,
            erhe::frame_pacing::Frame_time_recorder::now() - fence_wait_begin
        );
    }

    const std::lock_guard<std::mutex> frame_id_lock{m_frame_id_mutex};
    ERHE_VERIFY(frame_id == m_prepare_frame_id + 1);
    ERHE_VERIFY(frame_id >= m_render_frame_id);
    ERHE_VERIFY(frame_id - m_render_frame_id < static_cast<uint64_t>(s_max_frames_in_flight));
    const std::size_t  slot_index = get_frame_slot_index(frame_id);
    Device_frame_slot& slot       = m_frame_slots[slot_index];
    ERHE_VERIFY(slot.state == Device_frame_state::idle);

    m_prepare_frame_id = frame_id;
    slot.frame_id      = frame_id;
    slot.state         = Device_frame_state::waited;
    return true;
}

void Device_impl::drain_completed_frames(const uint64_t ended_frame_bound)
{
    // Drain frames whose cbs have signalled GPU completion via their
    // commit feedback (submit_command_buffers). Ring buffer ranges
    // pinned to those frames become safe to recycle; per-frame
    // completion handlers run now.
    //
    // Only frames that have ENDED (the frame index advanced past them)
    // are retired. A standalone mid-frame submit (lightmap bakes, ray
    // trace readback: allocate cb -> submit -> wait_idle inside the tick)
    // completes while its frame is still OPEN; retiring that frame here
    // would free every per-thread Command_buffer allocated for it -
    // including the tick cb the main loop is still recording into
    // (observed: lightmap G-buffer bake, then the next begin_swapchain
    // crashing on the freed Command_buffer). Deferred entries stay
    // pending and retire on the first drain after end_frame advances
    // the index.
    //
    // A frame retires once it has ended (frame < ended_frame_bound) and
    // every command buffer committed for it has completed, in frame id
    // order: the first frame still waiting on the GPU stops the walk.
    const std::lock_guard<std::mutex> run_lock{m_completion_run_mutex};
    m_retired_frames_scratch.clear();
    {
        std::lock_guard<std::mutex> lock{m_completion_mutex};
        while (m_latest_completed_frame < ended_frame_bound) {
            const uint64_t frame = m_latest_completed_frame;
            const auto i = std::find_if(
                m_frame_command_buffer_counts.begin(),
                m_frame_command_buffer_counts.end(),
                [frame](const Frame_command_buffer_count& entry) { return entry.frame_index == frame; }
            );
            if (i != m_frame_command_buffer_counts.end()) {
                if (i->completed < i->committed) {
                    break;
                }
                ERHE_VERIFY(i->completed == i->committed);
                m_frame_command_buffer_counts.erase(i);
            }
            m_retired_frames_scratch.push_back(frame);
            m_latest_completed_frame = frame + 1;
        }
    }
    for (const uint64_t completed_frame : m_retired_frames_scratch) {
        m_ring_buffer_pool.frame_completed(completed_frame);
        frame_completed(completed_frame);
    }
}

auto Device_impl::begin_frame() -> bool
{
    erhe::utility::Thread_roles::assert_render_thread();
    // The legacy shim acts on the oldest open frame.
    Device_frame_slot& slot = m_frame_slots[get_frame_slot_index(m_render_frame_id)];
    ERHE_VERIFY(slot.state == Device_frame_state::waited);
    ERHE_VERIFY(slot.frame_id == m_render_frame_id);

    drain_completed_frames(m_render_frame_id);

    slot.state = Device_frame_state::recording;
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
    return end_frame(m_render_frame_id);
}

auto Device_impl::end_frame(const uint64_t frame_id) -> bool
{
    // CONTRACT: end_frame ends the oldest open frame and drives per-frame
    // GPU-completion processing (frame_completed) for frames the GPU has
    // reported done. It does not submit, it does not present. See
    // doc/erhe/graphics.md ("Frame lifecycle") and the Vulkan
    // implementation, whose end_frame()/update_frame_completion() does the
    // same, for the rationale.
    erhe::utility::Thread_roles::assert_render_thread();
    ERHE_VERIFY(frame_id == m_render_frame_id);
    const std::size_t  slot_index = get_frame_slot_index(frame_id);
    Device_frame_slot& slot       = m_frame_slots[slot_index];
    ERHE_VERIFY(slot.frame_id == frame_id);
    ERHE_VERIFY(
        (slot.state == Device_frame_state::in_swapchain_frame) ||
        (slot.state == Device_frame_state::recording) ||
        (slot.state == Device_frame_state::waited)
    );

    // The CPU slot of the frame ends here, as in the Vulkan backend: the
    // frame pacer's CPU service time (doc/frame_pacing/inputs.md 3.2).
    m_device.get_frame_time_recorder().set(
        static_cast<std::int64_t>(frame_id),
        &erhe::frame_pacing::Frame_time_record::cpu_slot_end,
        erhe::frame_pacing::Frame_time_recorder::now()
    );

    // Recycle ring buffer ranges and fire completion handlers for any
    // frame the GPU has reported done. This must run from end_frame()
    // because the editor's per-frame loop calls end_frame() but never
    // begin_frame(); mirrors Vulkan's end_frame()/update_frame_completion(),
    // which drives frame_completed() the same way. Without this the
    // completion-handler queue (and m_pending_completed_frames) grow
    // unbounded and GPU->CPU readbacks (e.g. Id_renderer picking) never
    // complete. The render role reads its own frame id unlocked.
    drain_completed_frames(frame_id);

    {
        // The slot is idle before the frame id advances: once it has, the
        // main thread may acquire the slot for frame_id + N.
        const std::lock_guard<std::mutex> frame_id_lock{m_frame_id_mutex};
        slot.state = Device_frame_state::idle;
        ++m_render_frame_id;
    }
    if (frame_id == c_pipeline_startup_summary_frame) {
        m_pipeline_compiler->log_startup_summary();
    }
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
    return m_frame_slots[get_frame_slot_index(m_render_frame_id)].state == Device_frame_state::in_swapchain_frame;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

auto Device_impl::capture_last_frame(
    int&                      out_width,
    int&                      out_height,
    erhe::dataformat::Format& out_format,
    std::vector<std::byte>&   out_pixels
) -> bool
{
    if (m_surface == nullptr) {
        return false;
    }
    Swapchain* const swapchain = m_surface->get_impl().get_swapchain();
    if (swapchain == nullptr) {
        return false;
    }
    uint32_t width  = 0;
    uint32_t height = 0;
    if (!swapchain->get_impl().read_back_capture(width, height, out_pixels)) {
        return false;
    }
    out_width  = static_cast<int>(width);
    out_height = static_cast<int>(height);
    out_format = erhe::dataformat::Format::format_8_vec4_srgb;
    return true;
}

auto Device_impl::request_frame_capture() -> bool
{
    if (m_surface == nullptr) {
        return false;
    }
    Swapchain* const swapchain = m_surface->get_impl().get_swapchain();
    if (swapchain == nullptr) {
        return false;
    }
    Swapchain_impl& swapchain_impl = swapchain->get_impl();
    if (!swapchain_impl.is_capture_supported()) {
        return false;
    }
    swapchain_impl.request_capture();
    return true;
}

auto Device_impl::get_native_handles() const -> Native_device_handles
{
    return Native_device_handles{};
}

auto Device_impl::wait_for_displayed_frame(const std::int64_t frame_id, const uint64_t timeout_ns) -> Present_wait_result
{
    if (m_present_feedback == nullptr) {
        return Present_wait_result::unsupported;
    }
    return m_present_feedback->wait_for_displayed(frame_id, timeout_ns);
}

void Device_impl::set_present_target_time(const std::int64_t frame_id, const double target_time_seconds, const double hold_until_seconds)
{
    // The C15 holdback deadline is not used: macOS honors target present
    // times, and the presenting commit also carries the frame's GPU work,
    // which a holdback would delay.
    static_cast<void>(hold_until_seconds);
    erhe::utility::Thread_roles::assert_render_thread();
    m_present_target_frame_id     = frame_id;
    m_present_target_time_seconds = target_time_seconds;
}

Present_feedback::Present_feedback(erhe::frame_pacing::Frame_time_recorder& recorder)
    : m_recorder{&recorder}
{
}

void Present_feedback::note_present(const std::int64_t frame_id)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_newest_presented_id = std::max(m_newest_presented_id, frame_id);
}

void Present_feedback::on_presented(const std::int64_t frame_id, const double presented_time, const double target_time, const double period)
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (m_recorder == nullptr) {
            return; // the device is gone
        }
        if (presented_time <= 0.0) {
            ++m_undisplayed_streak;
            if (m_undisplayed_streak == s_undisplayed_log_count) {
                log_context->info(
                    "frame pacing: the last {} drawables were not displayed (window occluded or display asleep); no presentation feedback",
                    m_undisplayed_streak
                );
            }
        } else {
            if (m_undisplayed_streak >= s_undisplayed_log_count) {
                log_context->info("frame pacing: drawables are displayed again after {} that were not", m_undisplayed_streak);
            }
            m_undisplayed_streak = 0;
        }
        if (presented_time > 0.0) {
            // presentedTime is CACurrentMediaTime host time, the frame time
            // recorder's clock (measured identical).
            m_recorder->set(frame_id, &erhe::frame_pacing::Frame_time_record::achieved_present_time, presented_time);
            if ((target_time > 0.0) && (period > 0.0) && (m_targeted_count < s_early_check_count)) {
                ++m_targeted_count;
                if (presented_time < (target_time - (0.5 * period))) {
                    ++m_early_count;
                }
                if ((m_targeted_count == s_early_check_count) && (m_early_count > (s_early_check_count / 10))) {
                    log_context->warn(
                        "frame pacing: {} of {} targeted frames displayed a refresh or more before their target; "
                        "presentAtTime: is not honored on this system",
                        m_early_count, m_targeted_count
                    );
                }
            }
        }
        m_newest_displayed_id = std::max(m_newest_displayed_id, frame_id);
    }
    m_displayed.notify_all();
}

auto Present_feedback::wait_for_displayed(const std::int64_t frame_id, const uint64_t timeout_ns) -> Present_wait_result
{
    std::unique_lock<std::mutex> lock{m_mutex};
    if ((frame_id < 0) || (frame_id > m_newest_presented_id)) {
        return Present_wait_result::unsupported; // never presented; waiting could only time out
    }
    const bool displayed = m_displayed.wait_for(
        lock,
        std::chrono::nanoseconds{timeout_ns},
        [this, frame_id]() { return m_newest_displayed_id >= frame_id; }
    );
    return displayed ? Present_wait_result::displayed : Present_wait_result::timeout;
}

void Present_feedback::detach()
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_recorder            = nullptr;
        m_newest_displayed_id = std::numeric_limits<std::int64_t>::max(); // releases any waiter
    }
    m_displayed.notify_all();
}

auto Device_impl::get_frame_pacing_tier() const -> Frame_pacing_tier
{
    return m_frame_pacing_tier;
}

auto Device_impl::is_present_target_time_honored() const -> bool
{
    // Tier M requests targets with the drawable's presentAtTime:, which macOS
    // honors; the drawable is taken when the command buffer completes.
    return m_frame_pacing_tier == Frame_pacing_tier::full;
}

namespace {

// blit_framebuffer between formats: a full-screen triangle over the
// destination rectangle (the viewport), each fragment reading the source
// texel at its own framebuffer position plus the source - destination
// origin offset. read() returns the source in the float domain, and the
// pipeline's color attachment converts to the destination format.
constexpr const char* c_blit_framebuffer_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct Blit_vertex_out
{
    float4 position [[position]];
};

vertex Blit_vertex_out blit_framebuffer_vs(uint vertex_id [[vertex_id]])
{
    const float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    Blit_vertex_out out;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

fragment float4 blit_framebuffer_fs(
    Blit_vertex_out                in     [[stage_in]],
    texture2d<float, access::read> source [[texture(0)]],
    constant int2&                 offset [[buffer(0)]]
)
{
    return source.read(uint2(int2(in.position.xy) + offset));
}
)";

} // anonymous namespace

auto Device_impl::get_blit_framebuffer_pipeline(const unsigned long destination_pixel_format) -> MTL::RenderPipelineState*
{
    const std::lock_guard<std::mutex> lock{m_blit_framebuffer_mutex};
    for (const Blit_framebuffer_pipeline& entry : m_blit_framebuffer_pipelines) {
        if (entry.destination_pixel_format == destination_pixel_format) {
            return entry.pipeline;
        }
    }

    Scoped_transient_object_pool object_pool{}; // drains the strings created here
    std::string error;
    if (m_blit_framebuffer_library == nullptr) {
        m_blit_framebuffer_library = m_pipeline_compiler->new_library(c_blit_framebuffer_msl, "blit_framebuffer", error);
        if (m_blit_framebuffer_library == nullptr) {
            log_context->error("blit_framebuffer: shader library compile failed: {}", error);
            return nullptr;
        }
    }
    MTL4::LibraryFunctionDescriptor* const vertex_function   = m_pipeline_compiler->new_function_descriptor(m_blit_framebuffer_library, "blit_framebuffer_vs");
    MTL4::LibraryFunctionDescriptor* const fragment_function = m_pipeline_compiler->new_function_descriptor(m_blit_framebuffer_library, "blit_framebuffer_fs");
    ERHE_VERIFY((vertex_function != nullptr) && (fragment_function != nullptr));
    MTL4::RenderPipelineDescriptor* const descriptor = MTL4::RenderPipelineDescriptor::alloc()->init();
    descriptor->setLabel(NS::String::string("blit_framebuffer", NS::UTF8StringEncoding));
    descriptor->setVertexFunctionDescriptor  (vertex_function);
    descriptor->setFragmentFunctionDescriptor(fragment_function);
    descriptor->colorAttachments()->object(0)->setPixelFormat(static_cast<MTL::PixelFormat>(destination_pixel_format));
    MTL::RenderPipelineState* const pipeline = m_pipeline_compiler->new_render_pipeline(descriptor, error);
    descriptor->release();
    fragment_function->release();
    vertex_function->release();
    if (pipeline == nullptr) {
        log_context->error("blit_framebuffer: pipeline for pixel format {} failed: {}", destination_pixel_format, error);
        return nullptr;
    }
    m_blit_framebuffer_pipelines.push_back(
        Blit_framebuffer_pipeline{.destination_pixel_format = destination_pixel_format, .pipeline = pipeline}
    );
    return pipeline;
}

auto Device_impl::get_command_buffer(const unsigned int thread_slot) -> Command_buffer&
{
    ERHE_VERIFY(thread_slot < s_number_of_thread_slots);

    std::lock_guard<std::mutex> lock{m_per_thread_mutex};
    Per_thread_command_buffers::Allocated_command_buffer entry{};
    entry.frame_index    = get_frame_index();
    entry.command_buffer = std::make_unique<Command_buffer>(m_device);
    entry.command_buffer->get_impl().set_frame_id(entry.frame_index);

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

auto Device_impl::submit_command_buffers(std::span<Command_buffer* const> command_buffers) -> uint64_t
{
    Scoped_transient_object_pool object_pool{}; // drains what commit / present autorelease
    if (command_buffers.empty()) {
        return 0;
    }

    // CPU-side wait phase: any wait_for_cpu_completion_of(producer)
    // registered on the cbs in this batch blocks on the producer's
    // MTL::SharedEvent before anything is committed (outside the submit
    // mutex: the producer may be committed by another thread).
    for (Command_buffer* const command_buffer : command_buffers) {
        ERHE_VERIFY(command_buffer != nullptr);
        command_buffer->get_impl().pre_submit_wait();
    }

    // Commit each cb in submit order. The MTL4 queue starts work in commit
    // order; the producer and consumer barriers the encoders carry order it
    // (doc/erhe/metal4_backend.md "Encoders and barriers").
    uint64_t last_serial = 0;
    const std::unique_lock<std::mutex> submit_lock = lock_submit();
    for (Command_buffer* const command_buffer : command_buffers) {
        Command_buffer_impl& impl = command_buffer->get_impl();

        Swapchain_impl* const    swapchain = impl.get_swapchain_used();
        CA::MetalDrawable* const drawable  = (swapchain != nullptr) ? swapchain->get_current_drawable() : nullptr;

        // Queue waits of wait_for_gpu_completion_signal, before the commit
        // (the producer must have been committed before: the queue waits in
        // commit order).
        impl.encode_queue_waits(m_mtl_command_queue);

        // Read before end_recording, which clears it.
        const bool prefix_committed = impl.has_committed_drawable_prefix();
        Recording* const recording = impl.end_recording();
        if (recording == nullptr) {
            // begin() was not called; nothing to commit.
            impl.encode_queue_signals(m_mtl_command_queue);
            if (swapchain != nullptr) {
                swapchain->clear_current_drawable();
            }
            impl.clear_swapchain_used();
            continue;
        }

        // The drawable: the queue waits until the display no longer uses it
        // before this commit - the cb's work from the drawable's first use
        // on, the work before it was committed when the drawable was taken
        // (Command_buffer_impl::acquire_drawable_for_use) - and (below)
        // signals it after the commit, then presents it. Mirrors Vulkan's
        // swapchains_to_present handling at the end of submit.
        const std::int64_t present_frame_id = static_cast<std::int64_t>(impl.get_frame_id());
        const double       target_time      = (present_frame_id == m_present_target_frame_id) ? m_present_target_time_seconds : 0.0;
        const double       period           = m_device.get_display_refresh_duration_seconds();
        const bool         presents         = (drawable != nullptr);
        if (presents) {
            m_mtl_command_queue->wait(drawable);
            if (m_present_feedback != nullptr) {
                std::shared_ptr<Present_feedback> present_feedback = m_present_feedback;
                drawable->addPresentedHandler(
                    [present_feedback, present_frame_id, target_time, period](MTL::Drawable* presented) {
                        present_feedback->on_presented(present_frame_id, presented->presentedTime(), target_time, period);
                    }
                );
                present_feedback->note_present(present_frame_id);
            }
            // Count this cb against the presented-frame budget that
            // begin_swapchain waits on.
            note_presented_frame_submitted();
        }

        // The commit feedback (on_commit_feedback) gives the frame's GPU
        // bracket, resolves the timer samples, returns what the recording
        // held and counts the completion against the charged frame.
        recording->counts_frame     = true;
        recording->frame_index      = note_command_buffer_committed(impl.get_completion_frame_index());
        recording->presents           = presents;
        recording->has_present_prefix = presents && prefix_committed;
        recording->present_frame_id   = present_frame_id;

        // The present request of the frame time record: the commit and the
        // present hand the drawable over. They do not block (the
        // backpressure is in begin_swapchain), but the record carries the
        // request time like the Vulkan present does.
        erhe::frame_pacing::Frame_time_recorder& recorder = m_device.get_frame_time_recorder();
        if (presents) {
            recorder.set(present_frame_id, &erhe::frame_pacing::Frame_time_record::present_request_time, erhe::frame_pacing::Frame_time_recorder::now());
        }
        last_serial = commit_locked(*recording);
        impl.encode_queue_signals(m_mtl_command_queue);

        if (presents) {
            m_mtl_command_queue->signalDrawable(drawable);
            if ((target_time > 0.0) && (period > 0.0)) {
                // The compositor displays the drawable at the target vsync
                // when the request lies inside its request window, which
                // depends on the display mode (a ProMotion panel latches a
                // period earlier); the present calibration measured it.
                // Without one a quarter period early bias keeps a target on
                // the pacer's grid from rounding up to the next refresh
                // across a small grid phase error on a fixed-rate display
                // (doc/frame_pacing/capability_tiers.md section 5).
                const double request_offset = m_device.get_present_request_offset().value_or(0.25 * period);
                drawable->presentAtTime(target_time - request_offset);
            } else {
                drawable->present();
            }
            recorder.set(present_frame_id, &erhe::frame_pacing::Frame_time_record::present_return_time, erhe::frame_pacing::Frame_time_recorder::now());
        }
        if (swapchain != nullptr) {
            swapchain->clear_current_drawable();
        }
        impl.clear_swapchain_used();

        // GPU pre-warm (claim C26): ordered after the presenting command
        // buffer through the pre-warm event, so it runs in the idle gap.
        const double prewarm_seconds = ((swapchain != nullptr) && (m_gpu_prewarm != nullptr))
            ? m_device.get_present_prewarm(static_cast<std::int64_t>(impl.get_frame_id()))
            : 0.0;
        if (prewarm_seconds > 0.0) {
            m_gpu_prewarm->signal_after_last_commit();
            m_gpu_prewarm->dispatch_locked(prewarm_seconds);
        }
    }
    return last_serial;
}

void Device_impl::on_commit_feedback(Recording& recording, MTL4::CommitFeedback* const feedback)
{
    // A command buffer that faulted on the GPU completes with an error and
    // nothing else reports it. After repeated faults Metal ignores the
    // process's later submissions and rendering silently stops, so the
    // first error is the one worth seeing.
    const NS::Error* const error = (feedback != nullptr) ? feedback->error() : nullptr;
    if (error != nullptr) {
        const NS::String* const label = recording.command_buffer->label();
        log_context->error(
            "Metal command buffer '{}' of frame {} failed: {}",
            (label != nullptr) ? label->utf8String() : "",
            recording.frame_index,
            (error->localizedDescription() != nullptr) ? error->localizedDescription()->utf8String() : "(no description)"
        );
    }

    if (recording.on_feedback != nullptr) {
        recording.on_feedback(recording, feedback);
    }
    if (recording.presents || recording.present_prefix) {
        // GPUStartTime and GPUEndTime are host time, the recorder's clock.
        // The frame's GPU bracket spans its two commits when the work before
        // the drawable was committed separately.
        const double gpu_begin = (feedback != nullptr) ? feedback->GPUStartTime() : 0.0;
        const double gpu_end   = (feedback != nullptr) ? feedback->GPUEndTime()   : 0.0;
        if (recording.present_prefix) {
            merge_present_gpu_bracket(recording.present_frame_id, Present_gpu_bracket_part::prefix, gpu_begin, gpu_end);
        } else if (recording.has_present_prefix) {
            merge_present_gpu_bracket(recording.present_frame_id, Present_gpu_bracket_part::presenting, gpu_begin, gpu_end);
        } else {
            write_present_gpu_bracket(recording.present_frame_id, gpu_begin, gpu_end);
        }
    }
    if (recording.presents) {
        note_presented_frame_completed();
    }
    if (recording.gpu_timer_samples.needs_resolve()) {
        m_gpu_timer_sample_pool->resolve_command_buffer_samples(recording.gpu_timer_samples);
    }
    release_recording_payload(recording);
    const bool     counts_frame = recording.counts_frame;
    const uint64_t frame_index  = recording.frame_index;
    const uint64_t serial       = recording.serial;
    m_submission_pools->release_recording(&recording); // may be taken by another thread from here on
    if (counts_frame) {
        notify_command_buffer_completed(frame_index);
    }
    // Last: once the serial is gone, wait_idle / wait_for_commit may return
    // and the device may be destroyed.
    const std::lock_guard<std::mutex> lock{m_in_flight_mutex};
    std::erase(m_in_flight_serials, serial);
    m_in_flight_cv.notify_all();
}

void Device_impl::commit_drawable_prefix(Command_buffer_impl& command_buffer_impl, Recording& prefix)
{
    // The same steps submit_command_buffers takes before a commit: the CPU
    // waits, then under the submit mutex the queue waits registered so far,
    // then the commit, charged to the cb's frame. Queue waits and CPU waits
    // registered later apply to the rest of the cb, committed at submit.
    // Unlike at submit, the producers must have been committed already
    // (doc/erhe/metal4_backend.md "Submission"): fail loudly rather than
    // wait forever.
    Scoped_transient_object_pool object_pool{};
    {
        const std::unique_lock<std::mutex> verify_lock = lock_submit();
        command_buffer_impl.verify_waited_producers_committed();
    }
    command_buffer_impl.pre_submit_wait();
    const std::unique_lock<std::mutex> submit_lock = lock_submit();
    command_buffer_impl.encode_queue_waits(m_mtl_command_queue);
    prefix.counts_frame     = true;
    prefix.frame_index      = note_command_buffer_committed(command_buffer_impl.get_completion_frame_index());
    prefix.present_prefix   = true;
    prefix.present_frame_id = static_cast<std::int64_t>(command_buffer_impl.get_frame_id());
    static_cast<void>(commit_locked(prefix));
}

void Device_impl::write_present_gpu_bracket(const int64_t frame_id, const double gpu_begin, const double gpu_end)
{
    if ((gpu_begin <= 0.0) || (gpu_end <= gpu_begin)) {
        return;
    }
    if (m_clock_monitor != nullptr) {
        // Writes the bracket with the frame's GPU clock.
        m_clock_monitor->on_frame_gpu_done(frame_id, gpu_begin, gpu_end);
    } else {
        m_device.get_frame_time_recorder().set(
            frame_id,
            &erhe::frame_pacing::Frame_time_record::gpu_frame_begin, gpu_begin,
            &erhe::frame_pacing::Frame_time_record::gpu_frame_end,   gpu_end
        );
    }
}

void Device_impl::merge_present_gpu_bracket(const int64_t frame_id, const Present_gpu_bracket_part part, const double gpu_begin, const double gpu_end)
{
    Present_gpu_bracket complete{};
    {
        const std::lock_guard<std::mutex> lock{m_present_gpu_bracket_mutex};
        Present_gpu_bracket& bracket = m_present_gpu_brackets[static_cast<std::size_t>(frame_id) % m_present_gpu_brackets.size()];
        if (bracket.frame_id != frame_id) {
            bracket = Present_gpu_bracket{.frame_id = frame_id};
        }
        if (part == Present_gpu_bracket_part::prefix) {
            bracket.prefix_begin = gpu_begin;
            bracket.prefix_end   = gpu_end;
        } else {
            bracket.presenting_begin = gpu_begin;
            bracket.presenting_end   = gpu_end;
        }
        ++bracket.parts;
        if (bracket.parts < 2) {
            return;
        }
        complete = bracket;
        bracket  = Present_gpu_bracket{};
    }
    if ((complete.prefix_begin <= 0.0) || (complete.presenting_end <= complete.prefix_begin)) {
        return;
    }
    // Before the bracket: the observer takes the record as soon as
    // gpu_frame_end > gpu_frame_begin, and gpu_service_time() subtracts the
    // idle time (doc/frame_pacing/inputs.md: the pacer must not see its own
    // presentation wait as GPU load).
    const double idle = ((complete.presenting_begin > 0.0) && (complete.prefix_end > 0.0))
        ? std::max(0.0, complete.presenting_begin - complete.prefix_end)
        : 0.0;
    m_device.get_frame_time_recorder().set(frame_id, &erhe::frame_pacing::Frame_time_record::gpu_idle_time, idle);
    write_present_gpu_bracket(frame_id, complete.prefix_begin, complete.presenting_end);
}

void Device_impl::submit_command_buffer_and_wait(Command_buffer& command_buffer)
{
    // Commit the cb, then block until its feedback handler (and those of
    // earlier commits) has run - not for commits other threads made after it.
    Command_buffer* command_buffers[] = { &command_buffer };
    const uint64_t serial = submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    if (serial != 0) {
        wait_for_commit(serial);
    }
}

auto Device_impl::lock_submit() -> std::unique_lock<std::mutex>
{
    return std::unique_lock<std::mutex>{m_submit_mutex};
}

auto Device_impl::commit_locked(Recording& recording) -> uint64_t
{
    // Allocations created since the last commit become resident before any
    // command of this commit runs.
    m_residency->commit_if_changed();
    {
        const std::lock_guard<std::mutex> lock{m_in_flight_mutex};
        recording.serial = ++m_latest_commit_serial;
        m_in_flight_serials.push_back(recording.serial);
    }
    const uint64_t serial = recording.serial; // the feedback may return the recording before commit returns
    const MTL4::CommandBuffer* const command_buffers[] = { recording.command_buffer };
    // One MTL4::CommitOptions per commit: a CommitOptions created ahead of
    // time and kept with the recording for reuse delivered no feedback at
    // all (measured: the first commit of such an options object never
    // called its handler, and wait_idle hung). The handler is a plain block
    // over two pointers (no std::function).
    MTL4::CommitOptions* const options     = MTL4::CommitOptions::alloc()->init();
    Device_impl* const         device_impl = this;
    Recording* const           raw         = &recording;
    options->addFeedbackHandler(
        ^(MTL4::CommitFeedback* feedback) {
            device_impl->on_commit_feedback(*raw, feedback);
        }
    );
    m_mtl_command_queue->commit(command_buffers, 1, options);
    options->release();
    return serial;
}

void Device_impl::wait_for_commit(const uint64_t serial)
{
    std::unique_lock<std::mutex> lock{m_in_flight_mutex};
    m_in_flight_cv.wait(
        lock,
        [this, serial]() {
            return std::none_of(
                m_in_flight_serials.begin(),
                m_in_flight_serials.end(),
                [serial](const uint64_t in_flight) { return in_flight <= serial; }
            );
        }
    );
}

auto Device_impl::get_latest_commit_serial() -> uint64_t
{
    const std::lock_guard<std::mutex> lock{m_in_flight_mutex};
    return m_latest_commit_serial;
}

auto Device_impl::begin_internal_recording() -> Recording*
{
    return m_submission_pools->acquire_recording();
}

void Device_impl::release_recording_payload(Recording& recording)
{
    for (std::function<void()>& callback : recording.callbacks) {
        callback();
    }
    recording.callbacks.clear();
    for (NS::Object* const object : recording.retained_objects) {
        object->release();
    }
    recording.retained_objects.clear();
    for (MTL4::ArgumentTable* const argument_table : recording.argument_tables) {
        m_submission_pools->release_argument_table(argument_table);
    }
    recording.argument_tables.clear();
    for (MTL::Buffer* const chunk : recording.inline_bytes_chunks) {
        m_submission_pools->release_inline_bytes_chunk(chunk);
    }
    recording.inline_bytes_chunks.clear();
}

auto Device_impl::get_residency() -> Residency&
{
    return *m_residency;
}

auto Device_impl::get_submission_pools() -> Submission_pools&
{
    return *m_submission_pools;
}

auto Device_impl::get_pipeline_compiler() -> Pipeline_compiler&
{
    ERHE_VERIFY(m_pipeline_compiler); // null only once ~Device_impl wrote the archive
    return *m_pipeline_compiler;
}

auto Device_impl::get_pipeline_state_cache() -> Pipeline_state_cache&
{
    ERHE_VERIFY(m_pipeline_state_cache);
    return *m_pipeline_state_cache;
}

auto Device_impl::get_device() -> Device&
{
    return m_device;
}

void Device_impl::add_completion_handler(std::function<void(Device_impl&)> callback)
{
    const uint64_t frame_index = get_frame_index();
    std::lock_guard<std::mutex> lock{m_completion_handler_mutex};
    m_completion_handlers.emplace_back(frame_index, std::move(callback));
}

void Device_impl::frame_completed(const uint64_t completed_frame)
{
    // Called under m_completion_run_mutex, so the scratch is this call's.
    // Handlers of frames up to this one: frames retire in order, so an
    // earlier frame's handler is due as well.
    {
        std::lock_guard<std::mutex> lock{m_completion_handler_mutex};
        for (Completion_handler& entry : m_completion_handlers) {
            if (entry.frame_number <= completed_frame) {
                m_completion_handler_scratch.push_back(std::move(entry));
            }
        }
        std::erase_if(
            m_completion_handlers,
            [completed_frame](const Completion_handler& entry) { return entry.frame_number <= completed_frame; }
        );
    }
    for (const Completion_handler& entry : m_completion_handler_scratch) {
        entry.callback(*this);
    }
    m_completion_handler_scratch.clear();

    // Drop Command_buffers whose frame has retired on the GPU. A committed
    // cb has handed its recording to its commit feedback handler, so
    // dropping the wrapper here is safe even if some cbs from the same frame
    // committed late have not completed yet.
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

auto Device_impl::note_command_buffer_committed(const uint64_t begun_frame_index) -> uint64_t
{
    std::lock_guard<std::mutex> lock{m_completion_mutex};
    // Begun in a frame that has retired already (a long-lived recording
    // committed late): charged to the oldest frame not retired yet, so that
    // frame - and every later one, frames retire in order - waits for this
    // commit's feedback before its deferred releases free what the commit
    // may use.
    const uint64_t frame_index = std::max(begun_frame_index, m_latest_completed_frame);
    const auto i = std::find_if(
        m_frame_command_buffer_counts.begin(),
        m_frame_command_buffer_counts.end(),
        [frame_index](const Frame_command_buffer_count& entry) { return entry.frame_index == frame_index; }
    );
    if (i != m_frame_command_buffer_counts.end()) {
        ++i->committed;
    } else {
        m_frame_command_buffer_counts.push_back(Frame_command_buffer_count{.frame_index = frame_index, .committed = 1, .completed = 0});
    }
    return frame_index;
}

void Device_impl::notify_command_buffer_completed(const uint64_t frame_index)
{
    {
        std::lock_guard<std::mutex> lock{m_completion_mutex};
        const auto i = std::find_if(
            m_frame_command_buffer_counts.begin(),
            m_frame_command_buffer_counts.end(),
            [frame_index](const Frame_command_buffer_count& entry) { return entry.frame_index == frame_index; }
        );
        // Every commit counted by the feedback was charged to a frame that
        // cannot retire before this completion.
        ERHE_VERIFY(i != m_frame_command_buffer_counts.end());
        ++i->completed;
    }
    m_command_buffer_completed.notify_all();
}

void Device_impl::wait_for_frame_gpu_completion(const uint64_t frame_index)
{
    // A frame without an entry committed nothing, or has been retired.
    std::unique_lock<std::mutex> lock{m_completion_mutex};
    m_command_buffer_completed.wait(
        lock,
        [this, frame_index]() {
            const auto i = std::find_if(
                m_frame_command_buffer_counts.begin(),
                m_frame_command_buffer_counts.end(),
                [frame_index](const Frame_command_buffer_count& entry) { return entry.frame_index == frame_index; }
            );
            return (i == m_frame_command_buffer_counts.end()) || (i->completed >= i->committed);
        }
    );
}

auto Device_impl::wait_for_presented_frame_slot() -> bool
{
    // One refresh period of backpressure is normal (the compositor releases a
    // drawable per display refresh); anything past this timeout means
    // presentation is not progressing at all.
    constexpr std::chrono::milliseconds timeout{250};

    std::unique_lock<std::mutex> lock{m_presented_frames_mutex};
    const bool got_slot = m_presented_frames_cv.wait_for(
        lock,
        timeout,
        [this]() { return m_presented_frames_in_flight < s_max_frames_in_flight; }
    );
    if (!got_slot) {
        log_swapchain->warn(
            "presentation stalled: {} presented frames still in flight after {} ms; skipping this frame's rendering",
            m_presented_frames_in_flight,
            timeout.count()
        );
    }
    return got_slot;
}

void Device_impl::note_presented_frame_submitted()
{
    std::lock_guard<std::mutex> lock{m_presented_frames_mutex};
    ++m_presented_frames_in_flight;
}

void Device_impl::note_presented_frame_completed()
{
    {
        std::lock_guard<std::mutex> lock{m_presented_frames_mutex};
        if (m_presented_frames_in_flight > 0) {
            --m_presented_frames_in_flight;
        }
    }
    m_presented_frames_cv.notify_all();
}

auto Device_impl::recreate_surface_for_new_window() -> bool
{
    // Not applicable on Metal -- CAMetalLayer is recreated by the
    // platform layer; erhe's Surface wrapper is one-shot. Vulkan-only.
    return false;
}

void Device_impl::clear_render_pipeline_cache()
{
    // The cache entries retain their function descriptors, so a recompiled
    // shader never matches a stale entry; clearing after a reload releases
    // the pipelines of the replaced shaders (frame-deferred).
    m_pipeline_state_cache->clear();
}

void Device_impl::wait_idle()
{
    // Metal has no vkDeviceWaitIdle equivalent: wait until the feedback
    // handler of every commit made so far has run.
    if (m_mtl_command_queue == nullptr) {
        return;
    }
    // m_render_frame_id is the oldest open frame (or the next one); frames
    // before it have ended, so every command buffer of theirs is committed.
    // Read before the wait: a frame the render thread ends during the wait
    // may have committed command buffers after the wait's bound, which are
    // still running when the wait returns.
    uint64_t render_frame_id = 0;
    {
        const std::lock_guard<std::mutex> frame_id_lock{m_frame_id_mutex};
        render_frame_id = m_render_frame_id;
    }
    wait_for_commit(get_latest_commit_serial());

    // Every command buffer committed before the wait has completed, so every
    // frame that had ended before it is retired - the same statement the GL
    // backend makes after glFinish(). Its ring buffer ranges and completion
    // handlers are released before wait_idle returns (used by the editor
    // ctor's init-frame flow). Frames still open stay - see
    // drain_completed_frames.
    drain_completed_frames(render_frame_id);
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

// The render role (a dedicated render thread) gets the oldest open frame;
// every other caller the newest acquired frame while it is open, otherwise
// the next frame to be acquired.
auto Device_impl::get_frame_index() const -> uint64_t
{
    if (erhe::utility::Thread_roles::is_render_role()) {
        return m_render_frame_id;
    }
    const std::lock_guard<std::mutex> frame_id_lock{m_frame_id_mutex};
    return std::max(m_prepare_frame_id, m_render_frame_id);
}

auto Device_impl::is_frame_ended(const uint64_t frame) const -> bool
{
    // m_render_frame_id is the oldest open frame (or the next one to be
    // acquired): every frame before it has ended.
    const std::lock_guard<std::mutex> frame_id_lock{m_frame_id_mutex};
    return frame < m_render_frame_id;
}

auto Device_impl::get_number_of_frames_in_flight() const -> std::size_t
{
    return static_cast<std::size_t>(s_max_frames_in_flight);
}

auto Device_impl::is_frame_completed(const uint64_t frame) const -> bool
{
    std::lock_guard<std::mutex> lock{m_completion_mutex};
    return frame < m_latest_completed_frame;
}

auto Device_impl::get_debug_state() const -> Device_debug_state
{
    Device_debug_state state{};
    uint64_t render_frame_id = 0;
    {
        const std::lock_guard<std::mutex> lock{m_frame_id_mutex};
        render_frame_id            = m_render_frame_id;
        state.oldest_open_frame_id = m_render_frame_id;
        state.open_frame_count     = (m_prepare_frame_id >= m_render_frame_id) ? (m_prepare_frame_id - m_render_frame_id + 1) : 0;
    }
    {
        const std::lock_guard<std::mutex> lock{m_completion_mutex};
        state.unretired_frame_count = (render_frame_id > m_latest_completed_frame) ? (render_frame_id - m_latest_completed_frame) : 0;
        for (const Frame_command_buffer_count& entry : m_frame_command_buffer_counts) {
            if (entry.committed > entry.completed) {
                state.uncompleted_command_buffer_count += static_cast<std::size_t>(entry.committed - entry.completed);
            }
        }
    }
    {
        const std::lock_guard<std::mutex> lock{m_completion_handler_mutex};
        state.completion_handler_count = m_completion_handlers.size();
    }
    const Ring_buffer_pool_usage ring_buffer_usage = m_ring_buffer_pool.get_usage();
    state.ring_buffer_count        = ring_buffer_usage.buffer_count;
    state.busy_ring_buffer_count   = ring_buffer_usage.busy_buffer_count;
    state.ring_buffer_bytes_in_use = ring_buffer_usage.bytes_in_use;
    state.ring_buffer_capacity_bytes = ring_buffer_usage.capacity_bytes;
    return state;
}

auto Device_impl::allocate_ring_buffer_entry(
    const Buffer_target     buffer_target,
    const Ring_buffer_usage  usage,
    const std::size_t       byte_count
) -> Ring_buffer_range
{
    const std::size_t required_alignment = erhe::utility::next_power_of_two_16bit(get_buffer_alignment(buffer_target));
    return m_ring_buffer_pool.allocate(required_alignment, usage, byte_count);
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
    Format_properties properties{};

    // A format is usable on this Metal device only if it maps to a real
    // MTLPixelFormat. format_undefined and any format with no Metal equivalent
    // map to PixelFormatInvalid and report unsupported (all flags false), which
    // matches the Vulkan backend's VK_FORMAT_UNDEFINED early-out.
    const MTL::PixelFormat pixel_format = to_mtl_pixel_format(format);
    if (pixel_format == MTL::PixelFormatInvalid) {
        return properties;
    }
    properties.supported = true;

    // Depth/stencil renderability follows the format's aspect bits (per
    // erhe::dataformat); it is implicit for depth/stencil formats and is not
    // part of the color-side capability table. Mirrors the Vulkan backend.
    const std::size_t depth_bits   = erhe::dataformat::get_depth_size_bits(format);
    const std::size_t stencil_bits = erhe::dataformat::get_stencil_size_bits(format);
    const bool        is_color     = erhe::dataformat::has_color(format);

    properties.depth_renderable   = (depth_bits   > 0);
    properties.stencil_renderable = (stencil_bits > 0);

    // Color-side capabilities (color render target, linear filtering, blend and
    // MSAA) come from Apple's "Texture capabilities by pixel format" tables,
    // evaluated for this device's resolved GPU family. The table in
    // metal_pixel_format_table.{hpp,cpp} is a direct translation of
    // Metal-Feature-Set-Tables.pdf. Using it lets us correctly reject linear
    // filtering of integer and 32-bit-float formats, and blending of integer
    // formats -- all of which the previous "every color format is
    // filterable/blendable" heuristic reported wrongly.
    const std::uint64_t raw_pixel_format = static_cast<std::uint64_t>(pixel_format);
    const bool          format_in_table  = metal::metal_pixel_format_is_known(raw_pixel_format);
    const std::uint32_t caps =
        format_in_table
            ? metal::metal_pixel_format_texture_capabilities(raw_pixel_format, m_gpu_family)
            : metal::texture_capability_none;
    if (format_in_table) {
        properties.color_renderable  = (caps & metal::texture_capability_color)  != 0;
        properties.filter            = (caps & metal::texture_capability_filter) != 0;
        properties.framebuffer_blend = (caps & metal::texture_capability_blend)  != 0;
    } else {
        // Maps to a real MTLPixelFormat but is absent from the table (e.g. a
        // format added to to_mtl_pixel_format but not yet transcribed). Fall
        // back to the conservative aspect-based heuristic so behavior never
        // regresses below the previous implementation.
        properties.color_renderable  = is_color;
        properties.filter            = is_color;
        properties.framebuffer_blend = is_color;
    }

    // Runtime adjustment over the static table: the feature-set tables list the
    // 32-bit float color formats as non-filterable (Apple footnote: gated by
    // supports32BitFloatFiltering). On hardware that supports it -- every Apple
    // silicon GPU -- they are filterable, so report that rather than the
    // documented minimum. This is what MoltenVK does in modifyMTLFormatCapabilities.
    if (!properties.filter &&
        m_supports_32bit_float_filtering &&
        ((pixel_format == MTL::PixelFormatR32Float)  ||
         (pixel_format == MTL::PixelFormatRG32Float) ||
         (pixel_format == MTL::PixelFormatRGBA32Float))) {
        properties.filter = true;
    }

    properties.red_size         = static_cast<int>(erhe::dataformat::get_red_size_bits    (format));
    properties.green_size       = static_cast<int>(erhe::dataformat::get_green_size_bits  (format));
    properties.blue_size        = static_cast<int>(erhe::dataformat::get_blue_size_bits   (format));
    properties.alpha_size       = static_cast<int>(erhe::dataformat::get_alpha_size_bits  (format));
    properties.depth_size       = static_cast<int>(depth_bits);
    properties.stencil_size     = static_cast<int>(stencil_bits);
    properties.image_texel_size = static_cast<int>(erhe::dataformat::get_format_size_bytes(format));

    // MSAA sample counts. 1x is always present. A format is multisampleable if
    // the capability table grants MSAA, or -- for depth/stencil formats, whose
    // MSAA renderability is implicit -- if it is depth/stencil renderable.
    // Untabled but renderable formats keep the previous behavior.
    properties.texture_2d_sample_counts.push_back(1);
    const bool is_renderable =
        properties.color_renderable || properties.depth_renderable || properties.stencil_renderable;
    const bool multisampleable =
        properties.depth_renderable   ||
        properties.stencil_renderable ||
        ((caps & metal::texture_capability_msaa) != 0) ||
        (!format_in_table && is_renderable);
    if (multisampleable) {
        for (int count : {2, 4, 8}) {
            if (m_mtl_device->supportsTextureSampleCount(static_cast<NS::UInteger>(count))) {
                properties.texture_2d_sample_counts.push_back(count);
            }
        }
    }
    properties.texture_2d_array_max_width   = 16384;
    properties.texture_2d_array_max_height  = 16384;
    properties.texture_2d_array_max_layers  = 2048;
    return properties;
}

auto Device_impl::probe_image_format_support(const erhe::dataformat::Format format, const uint64_t usage_mask) const -> bool
{
    // Metal has no direct equivalent of vkGetPhysicalDeviceImageFormatProperties2,
    // so derive the answer from get_format_properties (which is itself grounded
    // in the MTLPixelFormat mapping and the format's aspect bits). An undefined /
    // unmappable format is never a usable image format -- matching the Vulkan
    // backend's VK_FORMAT_UNDEFINED -> false early-out.
    const Format_properties properties = get_format_properties(format);
    if (!properties.supported) {
        return false;
    }

    // Each requested attachment usage must be backed by the matching
    // renderability. Non-attachment usages (sampled, storage, transfer) are
    // available for any supported format on Metal 3.
    if ((usage_mask & Image_usage_flag_bit_mask::color_attachment) != 0) {
        if (!properties.color_renderable) {
            return false;
        }
    }
    if ((usage_mask & Image_usage_flag_bit_mask::depth_stencil_attachment) != 0) {
        if (!properties.depth_renderable && !properties.stencil_renderable) {
            return false;
        }
    }
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
    // Drop the formats this device cannot use as asked (get_format_properties
    // reads the capabilities of metal4_pixel_format_table.cpp), then order as
    // the Vulkan backend does: depth+stencil before depth-only, then higher
    // precision (more depth + stencil bits) first.
    const bool require_depth   = (sort_flags & format_flag_require_depth)   != 0;
    const bool require_stencil = (sort_flags & format_flag_require_stencil) != 0;
    std::erase_if(
        formats,
        [&](const erhe::dataformat::Format format) -> bool {
            const Format_properties properties = get_format_properties(format);
            if (!properties.supported) {
                return true;
            }
            if (require_depth && !properties.depth_renderable) {
                return true;
            }
            if (require_stencil && !properties.stencil_renderable) {
                return true;
            }
            if (requested_sample_count > 1) {
                const std::vector<int>& counts = properties.texture_2d_sample_counts;
                if (std::find(counts.begin(), counts.end(), requested_sample_count) == counts.end()) {
                    return true;
                }
            }
            return false;
        }
    );
    std::stable_sort(
        formats.begin(),
        formats.end(),
        [](const erhe::dataformat::Format lhs, const erhe::dataformat::Format rhs) {
            const std::size_t lhs_stencil = erhe::dataformat::get_stencil_size_bits(lhs);
            const std::size_t rhs_stencil = erhe::dataformat::get_stencil_size_bits(rhs);
            if ((lhs_stencil > 0) != (rhs_stencil > 0)) {
                return lhs_stencil > 0;
            }
            const std::size_t lhs_bits = erhe::dataformat::get_depth_size_bits(lhs) + lhs_stencil;
            const std::size_t rhs_bits = erhe::dataformat::get_depth_size_bits(rhs) + rhs_stencil;
            return lhs_bits > rhs_bits;
        }
    );
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

auto Device_impl::get_mtl_device() const -> MTL::Device*
{
    return m_mtl_device;
}

auto Device_impl::get_mtl_command_queue() const -> MTL4::CommandQueue*
{
    return m_mtl_command_queue;
}

auto Device_impl::get_default_sampler_state() const -> MTL::SamplerState*
{
    return m_default_sampler_state;
}

auto Device_impl::get_gpu_timer_sample_pool() -> Gpu_timer_sample_pool&
{
    return *m_gpu_timer_sample_pool;
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
