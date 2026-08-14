#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_swapchain.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace erhe::graphics {

Command_buffer_impl::Command_buffer_impl(Device& device, erhe::utility::Debug_label debug_label)
    : m_device     {&device}
    , m_debug_label{std::move(debug_label)}
{
}

Command_buffer_impl::~Command_buffer_impl() noexcept
{
    if (m_inter_encoder_fence != nullptr) {
        m_inter_encoder_fence->release();
        m_inter_encoder_fence = nullptr;
    }
    if (m_mtl_command_buffer != nullptr) {
        m_mtl_command_buffer->release();
        m_mtl_command_buffer = nullptr;
    }
    if (m_implicit_gpu_event != nullptr) {
        m_implicit_gpu_event->release();
        m_implicit_gpu_event = nullptr;
    }
    if (m_implicit_cpu_event != nullptr) {
        m_implicit_cpu_event->release();
        m_implicit_cpu_event = nullptr;
    }
}

Command_buffer_impl::Command_buffer_impl(Command_buffer_impl&& other) noexcept
    : m_device                          {other.m_device}
    , m_debug_label                     {std::move(other.m_debug_label)}
    , m_mtl_command_buffer              {other.m_mtl_command_buffer}
    , m_inter_encoder_fence             {other.m_inter_encoder_fence}
    , m_implicit_gpu_event              {other.m_implicit_gpu_event}
    , m_implicit_gpu_event_signal_value {other.m_implicit_gpu_event_signal_value}
    , m_implicit_cpu_event              {other.m_implicit_cpu_event}
    , m_implicit_cpu_event_signal_value {other.m_implicit_cpu_event_signal_value}
    , m_wait_for_cpu_pending            {std::move(other.m_wait_for_cpu_pending)}
    , m_signal_gpu_pending              {std::move(other.m_signal_gpu_pending)}
    , m_signal_cpu_pending              {std::move(other.m_signal_cpu_pending)}
    , m_swapchain_used                  {other.m_swapchain_used}
{
    other.m_device              = nullptr;
    other.m_mtl_command_buffer  = nullptr;
    other.m_inter_encoder_fence = nullptr;
    other.m_implicit_gpu_event  = nullptr;
    other.m_implicit_cpu_event  = nullptr;
    other.m_swapchain_used      = nullptr;
}

auto Command_buffer_impl::operator=(Command_buffer_impl&& other) noexcept -> Command_buffer_impl&
{
    if (this == &other) {
        return *this;
    }
    if (m_inter_encoder_fence != nullptr) {
        m_inter_encoder_fence->release();
    }
    if (m_mtl_command_buffer != nullptr) {
        m_mtl_command_buffer->release();
    }
    if (m_implicit_gpu_event != nullptr) {
        m_implicit_gpu_event->release();
    }
    if (m_implicit_cpu_event != nullptr) {
        m_implicit_cpu_event->release();
    }

    m_device                          = other.m_device;
    m_debug_label                     = std::move(other.m_debug_label);
    m_mtl_command_buffer              = other.m_mtl_command_buffer;
    m_inter_encoder_fence             = other.m_inter_encoder_fence;
    m_implicit_gpu_event              = other.m_implicit_gpu_event;
    m_implicit_gpu_event_signal_value = other.m_implicit_gpu_event_signal_value;
    m_implicit_cpu_event              = other.m_implicit_cpu_event;
    m_implicit_cpu_event_signal_value = other.m_implicit_cpu_event_signal_value;
    m_wait_for_cpu_pending            = std::move(other.m_wait_for_cpu_pending);
    m_signal_gpu_pending              = std::move(other.m_signal_gpu_pending);
    m_signal_cpu_pending              = std::move(other.m_signal_cpu_pending);
    m_swapchain_used                  = other.m_swapchain_used;

    other.m_device              = nullptr;
    other.m_mtl_command_buffer  = nullptr;
    other.m_inter_encoder_fence = nullptr;
    other.m_implicit_gpu_event  = nullptr;
    other.m_implicit_cpu_event  = nullptr;
    other.m_swapchain_used      = nullptr;
    return *this;
}

auto Command_buffer_impl::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Command_buffer_impl::begin()
{
    ERHE_VERIFY(m_device != nullptr);
    ERHE_VERIFY(m_mtl_command_buffer == nullptr);

    Device_impl& device_impl = m_device->get_impl();
    MTL::CommandQueue* queue = device_impl.get_mtl_command_queue();
    if (queue == nullptr) {
        return;
    }
    m_mtl_command_buffer = queue->commandBuffer();
    if (m_mtl_command_buffer == nullptr) {
        return;
    }
    m_mtl_command_buffer->retain();

    MTL::Device* mtl_device = device_impl.get_mtl_device();
    if ((mtl_device != nullptr) && (m_inter_encoder_fence == nullptr)) {
        m_inter_encoder_fence = mtl_device->newFence();
    }

    // Drive the existing per-frame completion plumbing: when the GPU
    // finishes consuming this cb, mark the cb's frame index as completed
    // so the next begin_frame() can recycle ring-buffer ranges pinned to
    // that frame. The handler runs on an arbitrary Metal dispatch
    // thread, so Device_impl::notify_command_buffer_completed takes the
    // completion mutex itself.
    const uint64_t frame_index_for_handler = device_impl.get_frame_index();
    Device_impl* device_impl_ptr = &device_impl;
    m_mtl_command_buffer->addCompletedHandler(
        [device_impl_ptr, frame_index_for_handler](MTL::CommandBuffer*) {
            device_impl_ptr->notify_command_buffer_completed(frame_index_for_handler);
        }
    );
}

void Command_buffer_impl::end()
{
    // Metal has no vkEndCommandBuffer equivalent; encoders endEncoding
    // themselves. The public Command_buffer's m_recording flag is the
    // state checkpoint callers care about.
}

auto Command_buffer_impl::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
    static_cast<void>(m_device);
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
    return true;
}

auto Command_buffer_impl::begin_swapchain(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
    Surface* surface = m_device->get_surface();
    if (surface == nullptr) {
        out_frame_state.should_render = false;
        return false;
    }
    Swapchain* swapchain = surface->get_swapchain();
    if (swapchain == nullptr) {
        out_frame_state.should_render = false;
        return false;
    }

    const bool ok = swapchain->get_impl().begin_frame(frame_begin_info);
    out_frame_state.should_render = ok;
    if (ok) {
        // Latch the engaged swapchain. submit_command_buffers reads this
        // and encodes presentDrawable on this cb before commit.
        m_swapchain_used = &swapchain->get_impl();
    }
    return ok;
}

void Command_buffer_impl::end_swapchain(const Frame_end_info& /*frame_end_info*/)
{
    // No submit yet; legacy end_frame still drives present.
}

void Command_buffer_impl::wait_for_cpu(Command_buffer& other)
{
    // Capture the current signal value: this cb's submit will block on
    // CPU until other's MTL::SharedEvent reaches that value, which
    // happens when other's GPU work completes (signal_cpu was registered
    // and fires via encodeSignalEvent at end-of-cb).
    Command_buffer_impl& other_impl = other.get_impl();
    Sync_entry entry{};
    entry.command_buffer = &other;
    entry.value          = other_impl.m_implicit_cpu_event_signal_value;
    m_wait_for_cpu_pending.push_back(entry);
}

void Command_buffer_impl::wait_for_gpu(Command_buffer& other)
{
    // Metal positional cb model: encodeWaitForEvent must land BEFORE
    // any encoder we record on this cb. Encode it now (callers are
    // expected to call wait_for_gpu after begin() and before any
    // make_render/compute/blit_command_encoder).
    ERHE_VERIFY(m_mtl_command_buffer != nullptr);
    Command_buffer_impl& other_impl = other.get_impl();
    if (other_impl.m_implicit_gpu_event != nullptr) {
        m_mtl_command_buffer->encodeWait(
            other_impl.m_implicit_gpu_event,
            other_impl.m_implicit_gpu_event_signal_value
        );
    }
}

void Command_buffer_impl::signal_gpu(Command_buffer& other)
{
    // Bump the target's GPU event signal value now so any subsequent
    // wait_for_gpu(other) reads the value this cb will signal. The
    // actual encodeSignalEvent is deferred until encode_synchronization
    // (must land after the cb's last encoder).
    Command_buffer_impl& other_impl = other.get_impl();
    other_impl.m_implicit_gpu_event_signal_value++;
    Sync_entry entry{};
    entry.command_buffer = &other;
    entry.value          = other_impl.m_implicit_gpu_event_signal_value;
    m_signal_gpu_pending.push_back(entry);
}

void Command_buffer_impl::signal_cpu(Command_buffer& other)
{
    // Same shape as signal_gpu but on the SharedEvent so CPU waiters
    // (wait_for_cpu) can observe completion.
    Command_buffer_impl& other_impl = other.get_impl();
    other_impl.m_implicit_cpu_event_signal_value++;
    Sync_entry entry{};
    entry.command_buffer = &other;
    entry.value          = other_impl.m_implicit_cpu_event_signal_value;
    m_signal_cpu_pending.push_back(entry);
}

void Command_buffer_impl::upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length)
{
    if ((data == nullptr) || (length == 0)) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer.get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }
    // Metal buffers in this backend use shared/managed storage with a
    // persistent CPU mapping, so the upload is just a memcpy. No
    // command recording is required and no GPU hazard is introduced
    // (callers serialize via Buffer's writable-range tracking).
    std::memcpy(static_cast<std::byte*>(mtl_buffer->contents()) + offset, data, length);
}

void Command_buffer_impl::upload_to_texture(
    const Texture&           texture,
    int                      level,
    int                      x,
    int                      y,
    int                      width,
    int                      height,
    erhe::dataformat::Format pixelformat,
    const void*              data,
    int                      row_stride
)
{
    if ((data == nullptr) || (width <= 0) || (height <= 0)) {
        return;
    }
    ERHE_VERIFY(m_device != nullptr);
    ERHE_VERIFY(m_mtl_command_buffer != nullptr);

    const Texture_impl& tex_impl = texture.get_impl();
    MTL::Texture* mtl_texture = tex_impl.get_mtl_texture();
    if (mtl_texture == nullptr) {
        m_device->device_message(
            Message_severity::error,
            "Metal upload_to_texture: texture has no MTL::Texture"
        );
        return;
    }

    Device_impl& device_impl = m_device->get_impl();
    MTL::Device* mtl_device  = device_impl.get_mtl_device();
    if (mtl_device == nullptr) {
        return;
    }

    // Block-compressed uploads go through Blit_command_encoder::copy_from_buffer
    ERHE_VERIFY(!erhe::dataformat::is_block_compressed(pixelformat));

    const std::size_t pixel_size = erhe::dataformat::get_format_size_bytes(pixelformat);
    const std::size_t src_stride = (row_stride > 0)
        ? static_cast<std::size_t>(row_stride)
        : (static_cast<std::size_t>(width) * pixel_size);
    const std::size_t data_size  = src_stride * static_cast<std::size_t>(height);

    MTL::Buffer* staging = mtl_device->newBuffer(data_size, MTL::ResourceStorageModeShared);
    if (staging == nullptr) {
        m_device->device_message(
            Message_severity::error,
            "Metal upload_to_texture: failed to allocate staging buffer"
        );
        return;
    }
    std::memcpy(staging->contents(), data, data_size);

    MTL::BlitCommandEncoder* blit = m_mtl_command_buffer->blitCommandEncoder();
    if (blit == nullptr) {
        staging->release();
        m_device->device_message(
            Message_severity::error,
            "Metal upload_to_texture: failed to create blit command encoder"
        );
        return;
    }

    if (m_inter_encoder_fence != nullptr) {
        blit->waitForFence(m_inter_encoder_fence);
    }

    blit->copyFromBuffer(
        staging,
        /*sourceOffset*/        NS::UInteger{0},
        /*sourceBytesPerRow*/   static_cast<NS::UInteger>(src_stride),
        /*sourceBytesPerImage*/ static_cast<NS::UInteger>(data_size),
        MTL::Size(
            static_cast<NS::UInteger>(width),
            static_cast<NS::UInteger>(height),
            NS::UInteger{1}
        ),
        mtl_texture,
        /*destinationSlice*/  NS::UInteger{0},
        /*destinationLevel*/  static_cast<NS::UInteger>(level),
        MTL::Origin(
            static_cast<NS::UInteger>(x),
            static_cast<NS::UInteger>(y),
            NS::UInteger{0}
        )
    );

    if (m_inter_encoder_fence != nullptr) {
        blit->updateFence(m_inter_encoder_fence);
    }
    blit->endEncoding();

    // Defer staging release to GPU completion of this cb.
    MTL::Buffer* staging_to_release = staging;
    m_mtl_command_buffer->addCompletedHandler(
        [staging_to_release](MTL::CommandBuffer*) {
            staging_to_release->release();
        }
    );
}

void Command_buffer_impl::clear_texture(const Texture& texture, std::array<double, 4> clear_value)
{
    ERHE_VERIFY(m_device != nullptr);
    ERHE_VERIFY(m_mtl_command_buffer != nullptr);

    MTL::Texture* mtl_texture = texture.get_impl().get_mtl_texture();
    if (mtl_texture == nullptr) {
        return;
    }

    const MTL::PixelFormat pixel_format = mtl_texture->pixelFormat();
    const bool is_depth =
        (pixel_format == MTL::PixelFormatDepth16Unorm) ||
        (pixel_format == MTL::PixelFormatDepth32Float) ||
        (pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) ||
        (pixel_format == MTL::PixelFormatDepth32Float_Stencil8);
    const bool is_stencil =
        (pixel_format == MTL::PixelFormatStencil8) ||
        (pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) ||
        (pixel_format == MTL::PixelFormatDepth32Float_Stencil8);

    if (is_depth || is_stencil) {
        // Depth/stencil clears require a render pass with LoadActionClear.
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
        MTL::RenderCommandEncoder* encoder = m_mtl_command_buffer->renderCommandEncoder(render_pass_desc);
        if ((encoder != nullptr) && (m_inter_encoder_fence != nullptr)) {
            encoder->waitForFence(
                m_inter_encoder_fence,
                MTL::RenderStageVertex | MTL::RenderStageFragment
            );
            encoder->updateFence(m_inter_encoder_fence, MTL::RenderStageFragment);
        }
        if (encoder != nullptr) {
            encoder->endEncoding();
        }
        render_pass_desc->release();
        return;
    }

    // Color textures: build a single-row clear pattern and either
    // replaceRegion (shared/managed) or staging-buffer + blit (private).
    const NS::UInteger width  = mtl_texture->width();
    const NS::UInteger height = mtl_texture->height();

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

    std::vector<uint8_t> clear_data(total_bytes);
    if (bytes_per_pixel == 16) {
        float* p = reinterpret_cast<float*>(clear_data.data());
        for (NS::UInteger i = 0; i < width * height; ++i) {
            p[i * 4 + 0] = static_cast<float>(clear_value[0]);
            p[i * 4 + 1] = static_cast<float>(clear_value[1]);
            p[i * 4 + 2] = static_cast<float>(clear_value[2]);
            p[i * 4 + 3] = static_cast<float>(clear_value[3]);
        }
    } else if ((bytes_per_pixel == 8) && (pixel_format == MTL::PixelFormatRGBA16Float)) {
        // RGBA16Float lacks an inline half-float conversion here; fall
        // back to zero-fill (matches the legacy behaviour).
        std::fill(clear_data.begin(), clear_data.end(), static_cast<uint8_t>(0));
    } else {
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

    if (mtl_texture->storageMode() == MTL::StorageModePrivate) {
        Device_impl& device_impl = m_device->get_impl();
        MTL::Device* mtl_device  = device_impl.get_mtl_device();
        if (mtl_device == nullptr) {
            return;
        }
        MTL::Buffer* staging = mtl_device->newBuffer(
            clear_data.data(), total_bytes, MTL::ResourceStorageModeShared
        );
        if (staging == nullptr) {
            return;
        }
        MTL::BlitCommandEncoder* blit = m_mtl_command_buffer->blitCommandEncoder();
        if (blit == nullptr) {
            staging->release();
            return;
        }
        if (m_inter_encoder_fence != nullptr) {
            blit->waitForFence(m_inter_encoder_fence);
        }
        blit->copyFromBuffer(
            staging, 0, bytes_per_row, 0,
            MTL::Size(width, height, 1),
            mtl_texture, 0, 0,
            MTL::Origin(0, 0, 0)
        );
        if (m_inter_encoder_fence != nullptr) {
            blit->updateFence(m_inter_encoder_fence);
        }
        blit->endEncoding();

        MTL::Buffer* staging_to_release = staging;
        m_mtl_command_buffer->addCompletedHandler(
            [staging_to_release](MTL::CommandBuffer*) {
                staging_to_release->release();
            }
        );
    } else {
        const MTL::Region region = MTL::Region(0, 0, width, height);
        mtl_texture->replaceRegion(region, 0, clear_data.data(), bytes_per_row);
    }
}

void Command_buffer_impl::transition_texture_layout(const Texture& /*texture*/, Image_layout /*new_layout*/)
{
    // Metal manages image layouts implicitly. Nothing to record here.
}

void Command_buffer_impl::cmd_texture_barrier(std::uint64_t /*usage_before*/, std::uint64_t /*usage_after*/)
{
    // Metal manages texture barriers implicitly via hazard tracking
    // (within a single MTL::CommandBuffer) and queue order (across
    // committed cbs). Aliased mip-view writes are caught by the
    // per-cb inter-encoder fence in Render_pass_impl /
    // Compute_command_encoder_impl / Blit_command_encoder_impl.
}

void Command_buffer_impl::memory_barrier(Memory_barrier_mask /*barriers*/)
{
    // Strong barrier. With the cb-per-Command_buffer model this is no
    // longer a cb-split: the user already separates work by submitting
    // multiple cbs (which Metal serializes in submission order on the
    // queue). Within a single cb, encoder ordering through the
    // inter-encoder fence covers the cases hazard tracking misses.
    //
    // If a future caller demands a hard ordering point inside one cb
    // here, encode an MTL::Event wait/signal via m_implicit_gpu_event.
    // Today no caller relies on that, so keep this as a no-op and let
    // the fence and cross-cb queue order carry the burden.
}

auto Command_buffer_impl::get_mtl_command_buffer() const noexcept -> MTL::CommandBuffer*
{
    return m_mtl_command_buffer;
}

auto Command_buffer_impl::get_inter_encoder_fence() const noexcept -> MTL::Fence*
{
    return m_inter_encoder_fence;
}

auto Command_buffer_impl::get_implicit_gpu_event() const noexcept -> MTL::Event*
{
    return m_implicit_gpu_event;
}

auto Command_buffer_impl::get_implicit_gpu_event_signal_value() const noexcept -> std::uint64_t
{
    return m_implicit_gpu_event_signal_value;
}

auto Command_buffer_impl::get_implicit_cpu_event() const noexcept -> MTL::SharedEvent*
{
    return m_implicit_cpu_event;
}

auto Command_buffer_impl::get_implicit_cpu_event_signal_value() const noexcept -> std::uint64_t
{
    return m_implicit_cpu_event_signal_value;
}

void Command_buffer_impl::set_implicit_sync(MTL::Event* gpu_event, MTL::SharedEvent* cpu_event) noexcept
{
    m_implicit_gpu_event = gpu_event;
    m_implicit_cpu_event = cpu_event;
}

void Command_buffer_impl::pre_submit_wait()
{
    // Block on each captured (target, value) pair. waitUntilSignaledValue
    // returns true on success / false on timeout; we use UINT64_MAX so
    // it effectively waits forever.
    for (const Sync_entry& entry : m_wait_for_cpu_pending) {
        ERHE_VERIFY(entry.command_buffer != nullptr);
        Command_buffer_impl& target_impl = entry.command_buffer->get_impl();
        if (target_impl.m_implicit_cpu_event == nullptr) {
            continue;
        }
        target_impl.m_implicit_cpu_event->waitUntilSignaledValue(entry.value, UINT64_MAX);
    }
    m_wait_for_cpu_pending.clear();
}

void Command_buffer_impl::encode_synchronization()
{
    if (m_mtl_command_buffer == nullptr) {
        m_signal_gpu_pending.clear();
        m_signal_cpu_pending.clear();
        return;
    }

    for (const Sync_entry& entry : m_signal_gpu_pending) {
        ERHE_VERIFY(entry.command_buffer != nullptr);
        Command_buffer_impl& target_impl = entry.command_buffer->get_impl();
        if (target_impl.m_implicit_gpu_event == nullptr) {
            continue;
        }
        m_mtl_command_buffer->encodeSignalEvent(target_impl.m_implicit_gpu_event, entry.value);
    }
    m_signal_gpu_pending.clear();

    for (const Sync_entry& entry : m_signal_cpu_pending) {
        ERHE_VERIFY(entry.command_buffer != nullptr);
        Command_buffer_impl& target_impl = entry.command_buffer->get_impl();
        if (target_impl.m_implicit_cpu_event == nullptr) {
            continue;
        }
        m_mtl_command_buffer->encodeSignalEvent(target_impl.m_implicit_cpu_event, entry.value);
    }
    m_signal_cpu_pending.clear();
}

auto Command_buffer_impl::get_swapchain_used() const noexcept -> Swapchain_impl*
{
    return m_swapchain_used;
}

void Command_buffer_impl::clear_swapchain_used() noexcept
{
    m_swapchain_used = nullptr;
}

} // namespace erhe::graphics
