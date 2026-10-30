#include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/metal4/metal4_compute_command_encoder.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_render_pass.hpp"
#include "erhe_graphics/metal4/metal4_swapchain.hpp"
#include "erhe_graphics/metal4/metal4_texture.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace erhe::graphics {

Command_buffer_impl::Command_buffer_impl(Device& device, erhe::utility::Debug_label debug_label)
    : m_device               {&device}
    , m_device_impl          {&device.get_impl()}
    , m_debug_label          {std::move(debug_label)}
    , m_gpu_timer_sample_pool{&device.get_impl().get_gpu_timer_sample_pool()}
{
}

Command_buffer_impl::~Command_buffer_impl() noexcept
{
    // A cb dropped without submit still holds its timer sample chunk and
    // records.
    if (m_gpu_timer_sample_pool != nullptr) {
        m_gpu_timer_sample_pool->discard_command_buffer_samples(m_gpu_timer_samples);
        m_gpu_timer_samples = Gpu_timer_command_buffer_samples{};
    }
    // A recording never committed: the GPU never saw it, so everything it
    // holds is released now.
    if (m_recording != nullptr) {
        m_recording->command_buffer->endCommandBuffer();
        m_device_impl->release_recording_payload(*m_recording);
        m_device_impl->get_submission_pools().release_recording(m_recording);
        m_recording = nullptr;
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

auto Command_buffer_impl::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Command_buffer_impl::get_frame_id() const noexcept -> std::uint64_t
{
    return m_frame_id;
}

auto Command_buffer_impl::get_completion_frame_index() const noexcept -> std::uint64_t
{
    return m_completion_frame_index;
}

void Command_buffer_impl::set_frame_id(const std::uint64_t frame_id) noexcept
{
    m_frame_id = frame_id;
}

void Command_buffer_impl::begin()
{
    Scoped_transient_object_pool object_pool{};
    ERHE_VERIFY(m_device_impl != nullptr);
    ERHE_VERIFY(m_recording == nullptr);
    ERHE_VERIFY(!m_drawable_prefix_committed);

    m_recording      = m_device_impl->get_submission_pools().acquire_recording();
    m_recording_used = false;
    if (!m_debug_label.empty()) {
        m_recording->command_buffer->setLabel(NS::String::string(m_debug_label.data(), NS::UTF8StringEncoding));
    }

    // Per-frame completion accounting (Device_impl::note_command_buffer_committed):
    // the commit feedback of this cb counts its completion against the frame
    // it was begun in; the frame retires once every cb committed for it has
    // completed.
    m_completion_frame_index = m_device_impl->get_frame_index();
}

void Command_buffer_impl::end()
{
    // Recording ends at submit (end_recording): encoders end themselves, and
    // the public Command_buffer's m_recording flag is the state checkpoint
    // callers care about.
}

auto Command_buffer_impl::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
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

    // Bound the number of presented-but-uncompleted frames before taking
    // another drawable. When presentation is stalled this returns false
    // rather than queueing yet another frame behind the stuck one - see
    // Device_impl::wait_for_presented_frame_slot. The wait is for earlier
    // frames' GPU completion, the fence wait of the frame time record; the
    // drawable itself is taken at its first use (acquire_drawable_for_use),
    // whose nextDrawable wait is the record's acquire - with a
    // display-synced layer the presentation backpressure the tier S slop
    // servo senses (doc/frame_pacing/inputs.md 3.2).
    erhe::frame_pacing::Frame_time_recorder& recorder   = m_device->get_frame_time_recorder();
    const std::int64_t                       record_id  = static_cast<std::int64_t>(m_frame_id);
    const double                             wait_begin = erhe::frame_pacing::Frame_time_recorder::now();
    const bool                               slot_ok    = m_device->get_impl().wait_for_presented_frame_slot();
    recorder.add(record_id, &erhe::frame_pacing::Frame_time_record::fence_wait_duration, erhe::frame_pacing::Frame_time_recorder::now() - wait_begin);
    if (!slot_ok) {
        out_frame_state.should_render = false;
        return false;
    }

    const bool ok = swapchain->get_impl().begin_frame(frame_begin_info);
    out_frame_state.should_render = ok;
    if (ok) {
        // Latch the engaged swapchain. submit_command_buffers reads this
        // and presents its drawable around the commit.
        m_swapchain_used = &swapchain->get_impl();
    }
    return ok;
}

void Command_buffer_impl::end_swapchain(const Frame_end_info& /*frame_end_info*/)
{
}

void Command_buffer_impl::signal_gpu_completion_to(Command_buffer& consumer)
{
    // Bump the consumer's GPU event signal value now so the consumer's
    // subsequent wait_for_gpu_completion_signal() reads the value this cb
    // will signal. The queue signals it right after this cb's commit.
    Command_buffer_impl& consumer_impl = consumer.get_impl();
    consumer_impl.m_implicit_gpu_event_signal_value++;
    Sync_entry entry{};
    entry.command_buffer = &consumer;
    entry.value          = consumer_impl.m_implicit_gpu_event_signal_value;
    m_signal_gpu_pending.push_back(entry);
}

void Command_buffer_impl::wait_for_gpu_completion_signal()
{
    // The MTL4 queue waits on this cb's own GPU event, at the value a
    // producer's signal_gpu_completion_to(this) bumped it to, right before
    // this cb is committed: none of its work starts before the producer's
    // signal.
    ERHE_VERIFY(m_recording != nullptr);
    if (m_implicit_gpu_event != nullptr) {
        m_gpu_waits_pending.push_back(m_implicit_gpu_event_signal_value);
    }
}

void Command_buffer_impl::signal_cpu_completion()
{
    // Same shape as signal_gpu_completion_to but on this cb's own
    // SharedEvent so CPU waiters (wait_for_cpu_completion_of(this)) can
    // observe completion. The value is captured now so that a wait
    // registered after this call reads it.
    m_implicit_cpu_event_signal_value++;
    m_signal_own_cpu_completion       = true;
    m_signal_own_cpu_completion_value = m_implicit_cpu_event_signal_value;
}

void Command_buffer_impl::wait_for_cpu_completion_of(Command_buffer& producer)
{
    // Capture the current signal value: this cb's submit will block on CPU
    // until producer's MTL::SharedEvent reaches that value, which the queue
    // signals once producer's GPU work completes.
    Command_buffer_impl& producer_impl = producer.get_impl();
    Sync_entry entry{};
    entry.command_buffer = &producer;
    entry.value          = producer_impl.m_implicit_cpu_event_signal_value;
    m_wait_for_cpu_pending.push_back(entry);
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
    // Metal buffers in this backend use shared storage with a persistent
    // CPU mapping, so the upload is just a memcpy. No command recording is
    // required (callers serialize via Buffer's writable-range tracking).
    std::memcpy(static_cast<std::byte*>(mtl_buffer->contents()) + offset, data, length);
}

auto Command_buffer_impl::try_upload_to_buffer(const Buffer& buffer, const std::size_t offset, const void* data, const std::size_t length, const std::size_t staging_capacity_byte_count) -> bool
{
    // Shared storage: upload_to_buffer is a memcpy and stages nothing, so
    // there is nothing to cap.
    static_cast<void>(staging_capacity_byte_count);
    upload_to_buffer(buffer, offset, data, length);
    return true;
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
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    if ((data == nullptr) || (width <= 0) || (height <= 0)) {
        return;
    }
    ERHE_VERIFY(m_device != nullptr);
    MTL4::CommandBuffer* const command_buffer = get_mtl4_command_buffer();
    ERHE_VERIFY(command_buffer != nullptr);

    const Texture_impl& tex_impl = texture.get_impl();
    MTL::Texture* mtl_texture = tex_impl.get_mtl_texture();
    if (mtl_texture == nullptr) {
        m_device->device_message(
            Message_severity::error,
            "Metal upload_to_texture: texture has no MTL::Texture"
        );
        return;
    }

    MTL::Device* mtl_device = m_device_impl->get_mtl_device();
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
    Residency& residency = m_device_impl->get_residency();
    residency.add(staging);

    // As Vulkan's transitions around the copy: every earlier access before
    // the copy, the copy before every shader read.
    MTL4::ComputeCommandEncoder* const blit = command_buffer->computeCommandEncoder();
    ERHE_VERIFY(blit != nullptr);
    encode_start_barriers(blit, c_all_stages, get_image_layout_stages(Image_layout::transfer_dst_optimal));
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
    encode_producer_barrier(blit, c_stage_blit, get_image_layout_stages(Image_layout::shader_read_only_optimal));
    blit->endEncoding();

    // The staging buffer lives until the GPU has completed this cb.
    add_completed_callback(
        [&residency, staging]() {
            residency.remove(staging);
            staging->release();
        }
    );
}

void Command_buffer_impl::clear_texture(const Texture& texture, std::array<double, 4> clear_value)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    ERHE_VERIFY(m_device != nullptr);
    MTL4::CommandBuffer* const command_buffer = get_mtl4_command_buffer();
    ERHE_VERIFY(command_buffer != nullptr);

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
        MTL4::RenderPassDescriptor* render_pass_desc = MTL4::RenderPassDescriptor::alloc()->init();
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
        // The clear is an attachment write of a render encoder: every earlier
        // access before it, the clear before every later access (Vulkan:
        // transfer_dst from the tracked layout, then the final layout).
        MTL4::RenderCommandEncoder* encoder = command_buffer->renderCommandEncoder(render_pass_desc);
        if (encoder != nullptr) {
            const std::uint64_t attachment_stages = get_image_usage_stages(Image_usage_flag_bit_mask::depth_stencil_attachment);
            encode_start_barriers(encoder, Render_encoder_use::transfer, c_all_stages, attachment_stages);
            encode_producer_barrier(encoder, attachment_stages, c_all_stages);
            encoder->endEncoding();
        }
        render_pass_desc->release();
        return;
    }

    // Color textures: build a clear pattern and either replaceRegion
    // (shared/managed) or staging-buffer + copy (private).
    const NS::UInteger width  = mtl_texture->width();
    const NS::UInteger height = mtl_texture->height();

    // One texel of the clear value in the texture's own format (packed,
    // half-float, sRGB-encoded, swizzled as the format defines), repeated
    // over the level.
    const erhe::dataformat::Format pixelformat     = texture.get_pixelformat();
    const NS::UInteger             bytes_per_pixel = static_cast<NS::UInteger>(erhe::dataformat::get_format_size_bytes(pixelformat));
    ERHE_VERIFY(!erhe::dataformat::is_block_compressed(pixelformat));
    ERHE_VERIFY((bytes_per_pixel > 0) && (bytes_per_pixel <= 16));
    const std::array<float, 4> clear_float{
        static_cast<float>(clear_value[0]),
        static_cast<float>(clear_value[1]),
        static_cast<float>(clear_value[2]),
        static_cast<float>(clear_value[3])
    };
    std::array<uint8_t, 16> clear_texel{};
    erhe::dataformat::convert(
        clear_float.data(), erhe::dataformat::Format::format_32_vec4_float,
        clear_texel.data(), pixelformat,
        1.0f
    );
    const NS::UInteger bytes_per_row = width * bytes_per_pixel;
    const NS::UInteger total_bytes   = bytes_per_row * height;

    std::vector<uint8_t> clear_data(total_bytes);
    for (NS::UInteger i = 0; i < width * height; ++i) {
        std::memcpy(clear_data.data() + (i * bytes_per_pixel), clear_texel.data(), bytes_per_pixel);
    }

    if (mtl_texture->storageMode() == MTL::StorageModePrivate) {
        MTL::Device* mtl_device = m_device_impl->get_mtl_device();
        if (mtl_device == nullptr) {
            return;
        }
        MTL::Buffer* staging = mtl_device->newBuffer(
            clear_data.data(), total_bytes, MTL::ResourceStorageModeShared
        );
        if (staging == nullptr) {
            return;
        }
        Residency& residency = m_device_impl->get_residency();
        residency.add(staging);
        MTL4::ComputeCommandEncoder* const blit = command_buffer->computeCommandEncoder();
        ERHE_VERIFY(blit != nullptr);
        encode_start_barriers(blit, c_all_stages, c_stage_blit);
        blit->copyFromBuffer(
            staging, 0, bytes_per_row, 0,
            MTL::Size(width, height, 1),
            mtl_texture, 0, 0,
            MTL::Origin(0, 0, 0)
        );
        encode_producer_barrier(blit, c_stage_blit, c_all_stages);
        blit->endEncoding();

        add_completed_callback(
            [&residency, staging]() {
                residency.remove(staging);
                staging->release();
            }
        );
    } else {
        const MTL::Region region = MTL::Region(0, 0, width, height);
        mtl_texture->replaceRegion(region, 0, clear_data.data(), bytes_per_row);
    }
}

void Command_buffer_impl::transition_texture_layout(const Texture& /*texture*/, const Image_layout new_layout)
{
    // MTL4 has no image layouts; the transition's dependency stays. Only the
    // destination is named: the source is every stage, as Vulkan's
    // transition from the tracked layout covers whatever accessed it.
    add_stage_barrier(c_all_stages, get_image_layout_stages(new_layout), Barrier_scope::command_buffer);
}

void Command_buffer_impl::add_stage_barrier(const std::uint64_t after_stages, const std::uint64_t before_stages, const Barrier_scope scope)
{
    if ((after_stages == 0) || (before_stages == 0)) {
        return; // no GPU producer or no GPU consumer on this queue
    }

    // With a compute encoder open (recorded on this thread into this cb) the
    // barrier is encoded there at once: the later dispatches of the encoder
    // wait - barrierAfterEncoderStages for the producer stages the encoder
    // itself runs (its earlier dispatches), the consumer barrier for the
    // rest (earlier encoders) and for earlier_submissions (earlier commits)
    // - and the producer barrier orders every later encoder.
    MTL4::ComputeCommandEncoder* const compute_encoder = Compute_command_encoder_impl::get_active_mtl_encoder();
    if (compute_encoder != nullptr) {
        const Command_buffer* const encoder_command_buffer = Compute_command_encoder_impl::get_active_command_buffer();
        ERHE_VERIFY((encoder_command_buffer != nullptr) && (&encoder_command_buffer->get_impl() == this));
        const std::uint64_t compute_before = before_stages & c_compute_encoder_stages;
        const std::uint64_t encoder_after  = after_stages  & c_compute_encoder_stages;
        if (compute_before != 0) {
            if (encoder_after != 0) {
                compute_encoder->barrierAfterEncoderStages(
                    static_cast<MTL::Stages>(encoder_after),
                    static_cast<MTL::Stages>(compute_before),
                    MTL4::VisibilityOptionDevice
                );
            }
            if ((scope == Barrier_scope::earlier_submissions) || (encoder_after != after_stages)) {
                encode_consumer_barrier(compute_encoder, after_stages, compute_before);
            }
        }
        encode_producer_barrier(compute_encoder, after_stages, before_stages);
        return;
    }

    // Between encoders: pending for the next encoder (encode_start_barriers),
    // whatever its kind.
    m_pending_barrier.after_stages  |= after_stages;
    m_pending_barrier.before_stages |= before_stages;
}

void Command_buffer_impl::encode_pending_barriers(
    MTL4::CommandEncoder* const encoder,
    const std::uint64_t         encoder_stages,
    const std::uint64_t         consumer_after_stages,
    const std::uint64_t         consumer_before_stages
)
{
    ERHE_VERIFY(encoder != nullptr);
    std::uint64_t consumer_after  = consumer_after_stages;
    std::uint64_t consumer_before = consumer_before_stages;
    const bool    has_pending     = (m_pending_barrier.before_stages != 0);
    // The pending barriers' consumers in this encoder: a consumer barrier
    // orders the current encoder only.
    if (has_pending && ((m_pending_barrier.before_stages & encoder_stages) != 0)) {
        consumer_after  |= m_pending_barrier.after_stages;
        consumer_before |= (m_pending_barrier.before_stages & encoder_stages);
    }
    if ((consumer_after != 0) && (consumer_before != 0)) {
        encode_consumer_barrier(encoder, consumer_after, consumer_before);
    }
    // Their consumers in every later encoder, of this cb and of later
    // commits: the producer barrier, which covers the work of the prior
    // encoders.
    if (has_pending) {
        encode_producer_barrier(encoder, m_pending_barrier.after_stages, m_pending_barrier.before_stages);
        m_pending_barrier = Pending_barrier{};
    }
}

void Command_buffer_impl::add_image_usage_barrier(const std::uint64_t usage_before, const std::uint64_t usage_after, const Barrier_scope scope)
{
    add_stage_barrier(get_image_usage_stages(usage_before), get_image_usage_stages(usage_after), scope);
}

void Command_buffer_impl::texture_barrier(
    const Texture&                   /*texture*/,
    const Texture_subresource_range& /*range*/,
    const std::uint64_t              usage_before,
    const std::uint64_t              usage_after,
    const Barrier_scope              scope
)
{
    // MTL4 has no per-resource barriers and no image layouts: the barrier is
    // a stage barrier, merged with the other pending ones.
    add_image_usage_barrier(usage_before, usage_after, scope);
}

void Command_buffer_impl::buffer_barrier(const Buffer& /*buffer*/, const std::uint64_t usage_before, const std::uint64_t usage_after, const Barrier_scope scope)
{
    add_stage_barrier(get_buffer_usage_stages(usage_before), get_buffer_usage_stages(usage_after), scope);
}

void Command_buffer_impl::encode_start_barriers(
    MTL4::RenderCommandEncoder* const encoder,
    const Render_encoder_use          use,
    const std::uint64_t               consumer_after_stages,
    const std::uint64_t               consumer_before_stages
)
{
    std::uint64_t consumer_before = consumer_before_stages;
    if ((use == Render_encoder_use::transfer) && ((m_pending_barrier.before_stages & c_stage_blit) != 0)) {
        // A transfer recorded as a render pass (blit_framebuffer's draw, a
        // depth / stencil clear) runs its transfer accesses in render
        // stages: a pending blit-stage consumer is ordered as them.
        consumer_before |= c_render_encoder_stages;
    }
    const std::uint64_t consumer_after = (consumer_before != consumer_before_stages)
        ? (consumer_after_stages | m_pending_barrier.after_stages)
        : consumer_after_stages;
    encode_pending_barriers(encoder, c_render_encoder_stages, consumer_after, consumer_before);
}

void Command_buffer_impl::encode_start_barriers(
    MTL4::ComputeCommandEncoder* const encoder,
    const std::uint64_t                consumer_after_stages,
    const std::uint64_t                consumer_before_stages
)
{
    encode_pending_barriers(encoder, c_compute_encoder_stages, consumer_after_stages, consumer_before_stages);
}

void Command_buffer_impl::end_recording_barriers()
{
    // A barrier still pending when the command buffer ends: its consumers
    // are in later commits. One empty compute encoder carries the producer
    // barrier (it covers the prior encoders and the later commits).
    if (m_pending_barrier.before_stages == 0) {
        return;
    }
    Scoped_transient_object_pool object_pool{};
    MTL4::ComputeCommandEncoder* const encoder = m_recording->command_buffer->computeCommandEncoder();
    ERHE_VERIFY(encoder != nullptr);
    encode_producer_barrier(encoder, m_pending_barrier.after_stages, m_pending_barrier.before_stages);
    encoder->endEncoding();
    m_pending_barrier = Pending_barrier{};
}

void Command_buffer_impl::cmd_texture_barrier(const std::uint64_t usage_before, const std::uint64_t usage_after)
{
    // user_synchronized only steers the render pass end barrier; the stages
    // are those of the usages (as Vulkan's cmd_texture_barrier).
    add_image_usage_barrier(
        usage_before & ~Image_usage_flag_bit_mask::user_synchronized,
        usage_after  & ~Image_usage_flag_bit_mask::user_synchronized,
        Barrier_scope::command_buffer
    );
}

void Command_buffer_impl::memory_barrier(const Memory_barrier_mask barriers)
{
    // Vulkan's memory_barrier: every earlier write of every stage before the
    // accesses the mask names. Pending for the next encoder or, inside an
    // open compute encoder of this thread, encoded there at once
    // (add_stage_barrier).
    add_stage_barrier(c_all_stages, get_memory_barrier_stages(barriers), Barrier_scope::command_buffer);
}

auto Command_buffer_impl::get_mtl4_command_buffer() noexcept -> MTL4::CommandBuffer*
{
    if (m_recording == nullptr) {
        return nullptr;
    }
    m_recording_used = true;
    return m_recording->command_buffer;
}

void Command_buffer_impl::acquire_drawable_for_use(Swapchain_impl& swapchain)
{
    if (!swapchain.is_drawable_pending()) {
        return; // headless, already taken this frame, or no frame begun
    }
    // MTL4 has no drawable wait inside a command buffer: the queue's wait
    // for the drawable (submit_command_buffers) holds back the whole commit
    // after it and every later commit. The work recorded before the
    // drawable's first use is committed now, before the CPU blocks in
    // nextDrawable, so the GPU runs it while the layer has no drawable free
    // (doc/erhe/metal4_backend.md "Present" has the measurement).
    if ((m_swapchain_used == &swapchain) && (m_recording != nullptr) && m_recording_used) {
        // Ending the command buffer with an encoder open would drop it.
        ERHE_VERIFY(Compute_command_encoder_impl::get_active_mtl_encoder() == nullptr);
        ERHE_VERIFY(Render_pass_impl::get_active_mtl_encoder() == nullptr);
        Scoped_transient_object_pool object_pool{};
        m_recording->command_buffer->endCommandBuffer();
        Recording* const prefix = m_recording;
        if (!m_debug_label.empty()) {
            // The commit error log names the faulting commit by its label.
            prefix->command_buffer->setLabel(
                NS::String::string(m_debug_label.data(), NS::UTF8StringEncoding)->stringByAppendingString(
                    NS::String::string(" (before drawable)", NS::UTF8StringEncoding)
                )
            );
        }

        m_recording           = m_device_impl->get_submission_pools().acquire_recording();
        m_recording_used      = false;
        m_inline_bytes_chunk  = nullptr; // the chunk being filled stays with the prefix
        m_inline_bytes_offset = 0;
        if (!m_debug_label.empty()) {
            m_recording->command_buffer->setLabel(NS::String::string(m_debug_label.data(), NS::UTF8StringEncoding));
        }
        m_device_impl->commit_drawable_prefix(*this, *prefix);
        m_drawable_prefix_committed = true;
    }

    // The acquire of the frame time record: the wait for a layer drawable.
    erhe::frame_pacing::Frame_time_recorder& recorder  = m_device->get_frame_time_recorder();
    const std::int64_t                       record_id = static_cast<std::int64_t>(m_frame_id);
    recorder.set_once(record_id, &erhe::frame_pacing::Frame_time_record::acquire_begin, erhe::frame_pacing::Frame_time_recorder::now());
    const bool acquired = swapchain.acquire_drawable();
    recorder.set_once(record_id, &erhe::frame_pacing::Frame_time_record::acquire_end, erhe::frame_pacing::Frame_time_recorder::now());
    if (!acquired) {
        log_swapchain->warn("Metal 4: the layer returned no drawable; the frame is not presented");
    }
}

void Command_buffer_impl::verify_waited_producers_committed() const
{
    // A producer committed after this cb's work before the drawable would
    // never be waited for: the queue (or the CPU) would wait forever. The
    // producer must be submitted before the consumer's swapchain pass, not
    // in the same submit_command_buffers batch after it.
    for (const Sync_entry& entry : m_wait_for_cpu_pending) {
        ERHE_VERIFY(entry.command_buffer != nullptr);
        ERHE_VERIFY(entry.value <= entry.command_buffer->get_impl().m_implicit_cpu_event_queued_value);
    }
    for (const std::uint64_t value : m_gpu_waits_pending) {
        ERHE_VERIFY(value <= m_implicit_gpu_event_queued_value);
    }
}

auto Command_buffer_impl::has_committed_drawable_prefix() const noexcept -> bool
{
    return m_drawable_prefix_committed;
}

auto Command_buffer_impl::acquire_argument_table() -> MTL4::ArgumentTable*
{
    ERHE_VERIFY(m_recording != nullptr);
    MTL4::ArgumentTable* const argument_table = m_device_impl->get_submission_pools().acquire_argument_table();
    m_recording->argument_tables.push_back(argument_table);
    return argument_table;
}

auto Command_buffer_impl::allocate_inline_bytes(const void* const data, const std::size_t byte_count) -> uint64_t
{
    constexpr std::size_t alignment  = Submission_pools::c_inline_bytes_alignment;
    constexpr std::size_t chunk_size = Submission_pools::c_inline_bytes_chunk_size;
    ERHE_VERIFY(byte_count <= chunk_size);
    std::size_t offset = ((m_inline_bytes_offset + alignment - 1) / alignment) * alignment;
    if ((m_inline_bytes_chunk == nullptr) || ((offset + byte_count) > chunk_size)) {
        m_inline_bytes_chunk = m_device_impl->get_submission_pools().acquire_inline_bytes_chunk();
        m_recording->inline_bytes_chunks.push_back(m_inline_bytes_chunk);
        offset = 0;
    }
    std::memcpy(static_cast<std::byte*>(m_inline_bytes_chunk->contents()) + offset, data, byte_count);
    m_inline_bytes_offset = offset + byte_count;
    return m_inline_bytes_chunk->gpuAddress() + offset;
}

void Command_buffer_impl::retain_until_completed(NS::Object* const object)
{
    ERHE_VERIFY(m_recording != nullptr);
    if (object != nullptr) {
        m_recording->retained_objects.push_back(object);
    }
}

void Command_buffer_impl::add_completed_callback(std::function<void()> callback)
{
    ERHE_VERIFY(m_recording != nullptr);
    m_recording->callbacks.push_back(std::move(callback));
}

void Command_buffer_impl::set_implicit_sync(MTL::Event* gpu_event, MTL::SharedEvent* cpu_event) noexcept
{
    m_implicit_gpu_event = gpu_event;
    m_implicit_cpu_event = cpu_event;
}

void Command_buffer_impl::pre_submit_wait()
{
    // Block on each captured (target, value) pair. waitUntilSignaledValue
    // returns true on success / false on timeout; UINT64_MAX waits forever.
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

void Command_buffer_impl::encode_queue_waits(MTL4::CommandQueue* const queue)
{
    ERHE_VERIFY(queue != nullptr);
    for (const std::uint64_t value : m_gpu_waits_pending) {
        queue->wait(m_implicit_gpu_event, value);
    }
    m_gpu_waits_pending.clear();
}

auto Command_buffer_impl::end_recording() -> Recording*
{
    if (m_recording == nullptr) {
        return nullptr;
    }
    Recording* const recording = m_recording;
    end_recording_barriers();
    recording->command_buffer->endCommandBuffer();
    recording->gpu_timer_samples = m_gpu_timer_samples;
    m_gpu_timer_samples   = Gpu_timer_command_buffer_samples{};
    m_recording                 = nullptr;
    m_recording_used            = false;
    m_drawable_prefix_committed = false;
    m_inline_bytes_chunk        = nullptr;
    m_inline_bytes_offset = 0;
    return recording;
}

void Command_buffer_impl::encode_queue_signals(MTL4::CommandQueue* const queue)
{
    ERHE_VERIFY(queue != nullptr);
    for (const Sync_entry& entry : m_signal_gpu_pending) {
        ERHE_VERIFY(entry.command_buffer != nullptr);
        Command_buffer_impl& target_impl = entry.command_buffer->get_impl();
        if (target_impl.m_implicit_gpu_event == nullptr) {
            continue;
        }
        queue->signalEvent(target_impl.m_implicit_gpu_event, entry.value);
        target_impl.m_implicit_gpu_event_queued_value = std::max(target_impl.m_implicit_gpu_event_queued_value, entry.value);
    }
    m_signal_gpu_pending.clear();

    if (m_signal_own_cpu_completion) {
        if (m_implicit_cpu_event != nullptr) {
            queue->signalEvent(m_implicit_cpu_event, m_signal_own_cpu_completion_value);
            m_implicit_cpu_event_queued_value = std::max(m_implicit_cpu_event_queued_value, m_signal_own_cpu_completion_value);
        }
        m_signal_own_cpu_completion = false;
    }
}

auto Command_buffer_impl::get_swapchain_used() const noexcept -> Swapchain_impl*
{
    return m_swapchain_used;
}

void Command_buffer_impl::clear_swapchain_used() noexcept
{
    m_swapchain_used = nullptr;
}

auto Command_buffer_impl::get_gpu_timer_samples() noexcept -> Gpu_timer_command_buffer_samples&
{
    return m_gpu_timer_samples;
}

auto Command_buffer_impl::get_gpu_timer_sample_pool() const noexcept -> Gpu_timer_sample_pool*
{
    return m_gpu_timer_sample_pool;
}

} // namespace erhe::graphics
