#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_surface.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include <cstring>
#include "erhe_utility/align.hpp"

#include <fmt/format.h>
#include <Metal/Metal.hpp>

namespace erhe::graphics {

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config)
    : m_device        {device}
    , m_shader_monitor{device}
{
    static_cast<void>(graphics_config);

    install_metal_error_handler(device);

    m_mtl_device = MTL::CreateSystemDefaultDevice();
    if (m_mtl_device == nullptr) {
        log_startup->error("Failed to create Metal device");
        return;
    }

    log_startup->info("Metal device: {}", m_mtl_device->name()->utf8String());

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

    // Metal coordinate conventions
    // Metal viewport transform uses y_w = (1 - y_ndc)/2 * h + originY, which
    // maps positive NDC Y to the top of the screen. No projection Y-flip needed.
    m_info.coordinate_conventions.native_depth_range = erhe::math::Depth_range::zero_to_one;
    m_info.coordinate_conventions.framebuffer_origin = erhe::math::Framebuffer_origin::top_left;
    m_info.coordinate_conventions.texture_origin     = erhe::math::Texture_origin::top_left;
    m_info.coordinate_conventions.clip_space_y_flip  = erhe::math::Clip_space_y_flip::disabled;

    // Enable debug groups for Metal GPU capture
    Scoped_debug_group::s_enabled = true;

    if (surface_create_info.context_window != nullptr) {
        m_surface = std::make_unique<Surface>(
            std::make_unique<Surface_impl>(*this, surface_create_info)
        );
    }
}

Device_impl::~Device_impl() noexcept
{
    m_surface.reset();
    if (m_texture_argument_encoder != nullptr) {
        m_texture_argument_encoder->release();
        m_texture_argument_encoder = nullptr;
    }
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

auto Device_impl::wait_frame(Frame_state& out_frame_state) -> bool
{
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
    return true;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    // Notify ring buffers that earlier frames have completed.
    // Use a 2-frame delay: assume the GPU has finished frame N-2 by the time
    // frame N begins. This is conservative and avoids needing per-command-buffer
    // completion handlers.
    if (m_frame_index > 2) {
        const uint64_t completed_frame = m_frame_index - 2;
        for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
            ring_buffer->frame_completed(completed_frame);
        }
    }

    bool result = true;
    if (m_surface) {
        Swapchain* swapchain = m_surface->get_swapchain();
        if (swapchain != nullptr) {
            result = swapchain->begin_frame(frame_begin_info);
        }
    }
    return result;
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    bool result = true;
    if (m_surface) {
        Swapchain* swapchain = m_surface->get_swapchain();
        if (swapchain != nullptr) {
            result = swapchain->end_frame(frame_end_info);
        }
    }
    ++m_frame_index;
    return result;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

void Device_impl::resize_swapchain_to_window()
{
    // TODO Implement Metal swapchain resize
}

void Device_impl::memory_barrier(const Memory_barrier_mask)
{
    // Metal synchronizes through command buffer ordering within the same queue.
    // Compute and render use separate command buffers submitted to the same queue,
    // so ordering is guaranteed. No additional barrier is needed.
}

void Device_impl::clear_texture(const Texture& texture, const std::array<double, 4> clear_value)
{
    MTL::Texture* mtl_texture = texture.get_impl().get_mtl_texture();
    if (mtl_texture == nullptr) {
        return;
    }

    MTL::PixelFormat pixel_format = mtl_texture->pixelFormat();
    bool is_depth =
        (pixel_format == MTL::PixelFormatDepth16Unorm) ||
        (pixel_format == MTL::PixelFormatDepth32Float) ||
        (pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) ||
        (pixel_format == MTL::PixelFormatDepth32Float_Stencil8);
    bool is_stencil =
        (pixel_format == MTL::PixelFormatStencil8) ||
        (pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) ||
        (pixel_format == MTL::PixelFormatDepth32Float_Stencil8);

    if (is_depth || is_stencil) {
        // Depth/stencil clears require a render pass with LoadActionClear
        if (m_mtl_command_queue == nullptr) {
            return;
        }
        MTL::CommandBuffer* command_buffer = m_mtl_command_queue->commandBuffer();
        if (command_buffer == nullptr) {
            return;
        }
        MTL::RenderPassDescriptor* render_pass_desc = MTL::RenderPassDescriptor::alloc()->init();
        if (is_depth) {
            MTL::RenderPassDepthAttachmentDescriptor* depth_att = render_pass_desc->depthAttachment();
            depth_att->setTexture(mtl_texture);
            depth_att->setLoadAction(MTL::LoadActionClear);
            depth_att->setClearDepth(clear_value[0]);
            depth_att->setStoreAction(MTL::StoreActionStore);
        }
        if (is_stencil) {
            MTL::RenderPassStencilAttachmentDescriptor* stencil_att = render_pass_desc->stencilAttachment();
            stencil_att->setTexture(mtl_texture);
            stencil_att->setLoadAction(MTL::LoadActionClear);
            stencil_att->setClearStencil(static_cast<uint32_t>(clear_value[0]));
            stencil_att->setStoreAction(MTL::StoreActionStore);
        }
        MTL::RenderCommandEncoder* encoder = command_buffer->renderCommandEncoder(render_pass_desc);
        encoder->endEncoding();
        command_buffer->commit();
        command_buffer->waitUntilCompleted();
        render_pass_desc->release();
        return;
    }

    {
        // For color textures, use replaceRegion to write clear color directly.
        // This avoids requiring MTLTextureUsageRenderTarget on the texture.

        const NS::UInteger width  = mtl_texture->width();
        const NS::UInteger height = mtl_texture->height();

        // Determine bytes per pixel from the Metal pixel format
        NS::UInteger bytes_per_pixel = 0;
        switch (pixel_format) {
            case MTL::PixelFormatR8Unorm:
            case MTL::PixelFormatA8Unorm:
                bytes_per_pixel = 1;
                break;
            case MTL::PixelFormatRG8Unorm:
            case MTL::PixelFormatR16Float:
                bytes_per_pixel = 2;
                break;
            case MTL::PixelFormatRGBA8Unorm:
            case MTL::PixelFormatRGBA8Unorm_sRGB:
            case MTL::PixelFormatBGRA8Unorm:
            case MTL::PixelFormatBGRA8Unorm_sRGB:
            case MTL::PixelFormatRGB10A2Unorm:
            case MTL::PixelFormatRG16Float:
            case MTL::PixelFormatR32Float:
                bytes_per_pixel = 4;
                break;
            case MTL::PixelFormatRGBA16Float:
            case MTL::PixelFormatRG32Float:
                bytes_per_pixel = 8;
                break;
            case MTL::PixelFormatRGBA32Float:
                bytes_per_pixel = 16;
                break;
            default:
                bytes_per_pixel = 4;
                break;
        }
        const NS::UInteger bytes_per_row = width * bytes_per_pixel;
        const NS::UInteger total_bytes   = bytes_per_row * height;

        // Build one row of clear color pixels, then replaceRegion row by row
        // or all at once for the full texture
        std::vector<uint8_t> clear_data(total_bytes);
        // Convert clear_value to the appropriate pixel representation
        // For float formats, write float values; for unorm, write uint8
        if (bytes_per_pixel == 16) {
            // RGBA32Float
            float* p = reinterpret_cast<float*>(clear_data.data());
            for (NS::UInteger i = 0; i < width * height; ++i) {
                p[i * 4 + 0] = static_cast<float>(clear_value[0]);
                p[i * 4 + 1] = static_cast<float>(clear_value[1]);
                p[i * 4 + 2] = static_cast<float>(clear_value[2]);
                p[i * 4 + 3] = static_cast<float>(clear_value[3]);
            }
        } else if (bytes_per_pixel == 8 && pixel_format == MTL::PixelFormatRGBA16Float) {
            // RGBA16Float - use half-float conversion via Metal's __fp16 or manual
            // Simplified: write as 32-bit floats then convert
            // For now, fall back to zero-fill for half-float formats
            std::fill(clear_data.begin(), clear_data.end(), static_cast<uint8_t>(0));
        } else {
            // Treat as RGBA8 unorm or similar byte-per-channel format
            const uint8_t r = static_cast<uint8_t>(std::clamp(clear_value[0], 0.0, 1.0) * 255.0 + 0.5);
            const uint8_t g = static_cast<uint8_t>(std::clamp(clear_value[1], 0.0, 1.0) * 255.0 + 0.5);
            const uint8_t b = static_cast<uint8_t>(std::clamp(clear_value[2], 0.0, 1.0) * 255.0 + 0.5);
            const uint8_t a = static_cast<uint8_t>(std::clamp(clear_value[3], 0.0, 1.0) * 255.0 + 0.5);
            for (NS::UInteger i = 0; i < width * height; ++i) {
                const NS::UInteger base = i * bytes_per_pixel;
                if (bytes_per_pixel >= 1) clear_data[base + 0] = r;
                if (bytes_per_pixel >= 2) clear_data[base + 1] = g;
                if (bytes_per_pixel >= 3) clear_data[base + 2] = b;
                if (bytes_per_pixel >= 4) clear_data[base + 3] = a;
            }
        }

        MTL::Region region = MTL::Region(0, 0, width, height);

        // replaceRegion only works for shared/managed storage.
        // For private storage, copy via a staging buffer + blit encoder.
        if (mtl_texture->storageMode() == MTL::StorageModePrivate) {
            if (m_mtl_command_queue == nullptr) {
                return;
            }
            MTL::CommandBuffer* command_buffer = m_mtl_command_queue->commandBuffer();
            if (command_buffer == nullptr) {
                return;
            }
            MTL::Buffer* staging = m_mtl_device->newBuffer(
                clear_data.data(), total_bytes, MTL::ResourceStorageModeShared
            );
            MTL::BlitCommandEncoder* blit = command_buffer->blitCommandEncoder();
            blit->copyFromBuffer(
                staging, 0, bytes_per_row, 0,
                MTL::Size(width, height, 1),
                mtl_texture, 0, 0,
                MTL::Origin(0, 0, 0)
            );
            blit->endEncoding();
            command_buffer->commit();
            command_buffer->waitUntilCompleted();
            staging->release();
        } else {
            mtl_texture->replaceRegion(region, 0, clear_data.data(), bytes_per_row);
        }
    }
}

void Device_impl::upload_to_buffer(const Buffer& buffer, const size_t offset, const void* data, const size_t length)
{
    if ((data == nullptr) || (length == 0)) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer.get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }
    std::memcpy(static_cast<std::byte*>(mtl_buffer->contents()) + offset, data, length);
}

void Device_impl::upload_to_texture(const Texture&, int, int, int, int, int, erhe::dataformat::Format, const void*, int)
{
    // Metal textures use StorageModePrivate; use blit command encoder for uploads
    m_device.device_error("Metal: upload_to_texture should not be called; use blit command encoder");
}

void Device_impl::add_completion_handler(std::function<void()>)
{
    ERHE_FATAL("Metal: add_completion_handler not implemented");
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

auto Device_impl::create_dummy_texture(const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
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

void Device_impl::initialize_frame_capture() {}
void Device_impl::start_frame_capture() {}
void Device_impl::end_frame_capture() {}

void Device_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    // No-op for Metal -- image layouts are managed by the runtime
    static_cast<void>(texture);
    static_cast<void>(new_layout);
}

auto Device_impl::make_blit_command_encoder() -> Blit_command_encoder
{
    return Blit_command_encoder{m_device};
}

auto Device_impl::make_compute_command_encoder() -> Compute_command_encoder
{
    return Compute_command_encoder{m_device};
}

auto Device_impl::make_render_command_encoder() -> Render_command_encoder
{
    return Render_command_encoder{m_device};
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

auto Device_impl::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    return std::vector<erhe::dataformat::Format>{
        erhe::dataformat::Format::format_d32_sfloat
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
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
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
