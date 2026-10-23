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

    MTL4::ComputeCommandEncoder* const blit = command_buffer->computeCommandEncoder();
    ERHE_VERIFY(blit != nullptr);
    encode_start_barriers(blit);
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
        MTL4::RenderCommandEncoder* encoder = command_buffer->renderCommandEncoder(render_pass_desc);
        if (encoder != nullptr) {
            encode_start_barriers(encoder, Render_encoder_use::transfer);
            encoder->endEncoding();
        }
        render_pass_desc->release();
        return;
    }

    // Color textures: build a clear pattern and either replaceRegion
    // (shared/managed) or staging-buffer + copy (private).
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
        encode_start_barriers(blit);
        blit->copyFromBuffer(
            staging, 0, bytes_per_row, 0,
            MTL::Size(width, height, 1),
            mtl_texture, 0, 0,
            MTL::Origin(0, 0, 0)
        );
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

void Command_buffer_impl::transition_texture_layout(const Texture& /*texture*/, Image_layout /*new_layout*/)
{
    // Metal manages image layouts implicitly. Nothing to record here.
}

namespace {

constexpr std::uint64_t c_render_encoder_stages =
    MTL::StageVertex | MTL::StageFragment | MTL::StageTile | MTL::StageObject | MTL::StageMesh;
constexpr std::uint64_t c_compute_encoder_stages =
    MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure;

// The MTL::Stages of the shader accesses of one barrier side: the stages its
// vertex_shader / fragment_shader / compute_shader qualifiers name, every
// shader stage without one. Image and buffer qualifiers share their bits.
[[nodiscard]] auto get_barrier_shader_stages(const std::uint64_t usage) -> std::uint64_t
{
    static_assert(Image_usage_flag_bit_mask::vertex_shader   == Buffer_usage_flag_bit_mask::vertex_shader);
    static_assert(Image_usage_flag_bit_mask::fragment_shader == Buffer_usage_flag_bit_mask::fragment_shader);
    static_assert(Image_usage_flag_bit_mask::compute_shader  == Buffer_usage_flag_bit_mask::compute_shader);
    if ((usage & Image_usage_flag_bit_mask::shader_stages) == 0) {
        return MTL::StageVertex | MTL::StageFragment | MTL::StageDispatch;
    }
    std::uint64_t stages = 0;
    if ((usage & Image_usage_flag_bit_mask::vertex_shader) != 0) {
        stages |= MTL::StageVertex;
    }
    if ((usage & Image_usage_flag_bit_mask::fragment_shader) != 0) {
        stages |= MTL::StageFragment;
    }
    if ((usage & Image_usage_flag_bit_mask::compute_shader) != 0) {
        stages |= MTL::StageDispatch;
    }
    return stages;
}

[[nodiscard]] auto get_image_usage_stages(const std::uint64_t usage) -> std::uint64_t
{
    std::uint64_t stages = 0;
    if ((usage & (Image_usage_flag_bit_mask::transfer_src | Image_usage_flag_bit_mask::transfer_dst)) != 0) {
        stages |= MTL::StageBlit;
    }
    if ((usage & (Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::storage | Image_usage_flag_bit_mask::input_attachment)) != 0) {
        stages |= get_barrier_shader_stages(usage);
    }
    if ((usage & (Image_usage_flag_bit_mask::color_attachment | Image_usage_flag_bit_mask::depth_stencil_attachment)) != 0) {
        // Attachment loads, depth / stencil tests, stores and resolves all
        // happen in the fragment stage of the pass (tile memory loads at its
        // start, stores and resolves at its end), so the fragment stage is the
        // producer and the consumer stage of an attachment access.
        stages |= MTL::StageFragment;
    }
    // none, present (the drawable is ordered by the queue's drawable wait),
    // host_transfer and fragment_density_map are no GPU stage of this queue;
    // discard changes nothing here (no layouts).
    return stages;
}

[[nodiscard]] auto get_buffer_usage_stages(const std::uint64_t usage) -> std::uint64_t
{
    std::uint64_t stages = 0;
    if ((usage & (Buffer_usage_flag_bit_mask::vertex | Buffer_usage_flag_bit_mask::index)) != 0) {
        stages |= MTL::StageVertex;
    }
    if ((usage & Buffer_usage_flag_bit_mask::indirect) != 0) {
        // Draw arguments (and indirect command buffer ranges) are read at the
        // vertex stage, dispatch arguments at the dispatch stage; like Vulkan's
        // DRAW_INDIRECT, the access is not qualified by a shader stage.
        stages |= MTL::StageVertex | MTL::StageDispatch;
    }
    const std::uint64_t shader_accesses =
        Buffer_usage_flag_bit_mask::uniform       |
        Buffer_usage_flag_bit_mask::storage_read  |
        Buffer_usage_flag_bit_mask::storage_write |
        Buffer_usage_flag_bit_mask::acceleration_structure_read;
    if ((usage & shader_accesses) != 0) {
        stages |= get_barrier_shader_stages(usage);
    }
    if ((usage & (Buffer_usage_flag_bit_mask::transfer_src | Buffer_usage_flag_bit_mask::transfer_dst)) != 0) {
        stages |= MTL::StageBlit;
    }
    const std::uint64_t build_accesses =
        Buffer_usage_flag_bit_mask::acceleration_structure_build_input |
        Buffer_usage_flag_bit_mask::acceleration_structure_read        |
        Buffer_usage_flag_bit_mask::acceleration_structure_write;
    if ((usage & build_accesses) != 0) {
        stages |= MTL::StageAccelerationStructure;
    }
    // host_read / host_write: ordered by command buffer completion and commit.
    return stages;
}

void encode_queue_stage_barrier(MTL4::CommandEncoder* const encoder, const std::uint64_t after_stages, const std::uint64_t before_stages)
{
    encoder->barrierAfterQueueStages(
        static_cast<MTL::Stages>(after_stages),
        static_cast<MTL::Stages>(before_stages),
        MTL4::VisibilityOptionDevice
    );
}

} // anonymous namespace

void Command_buffer_impl::add_barrier(const std::uint64_t after_stages, const std::uint64_t before_stages, const Barrier_scope scope)
{
    if ((after_stages == 0) || (before_stages == 0)) {
        return; // no GPU producer or no GPU consumer on this queue
    }
    const std::uint64_t render_before  = before_stages & c_render_encoder_stages;
    const std::uint64_t compute_before = before_stages & c_compute_encoder_stages;

    // Every barrier waits for the next encoder of its consumer's kind, which
    // orders the consumers recorded in later encoders.
    if (render_before != 0) {
        m_pending_render_barrier.after_stages  |= after_stages;
        m_pending_render_barrier.before_stages |= render_before;
    }
    if (compute_before != 0) {
        m_pending_compute_barrier.after_stages  |= after_stages;
        m_pending_compute_barrier.before_stages |= compute_before;
    }

    // With a compute encoder open (recorded on this thread into this cb) a
    // compute consumer may also be a later dispatch of the same encoder:
    // order it there at once. barrierAfterEncoderStages orders against the
    // encoder's own earlier dispatches; for earlier_submissions the producer
    // is in an earlier commit, which the consumer barrier on the queue
    // (barrierAfterQueueStages) orders. Both are encoded for that scope:
    // Apple documents the queue barrier for work before the current encoder
    // only.
    MTL4::ComputeCommandEncoder* const compute_encoder = Compute_command_encoder_impl::get_active_mtl_encoder();
    if ((compute_encoder != nullptr) && (compute_before != 0)) {
        const Command_buffer* const encoder_command_buffer = Compute_command_encoder_impl::get_active_command_buffer();
        ERHE_VERIFY((encoder_command_buffer != nullptr) && (&encoder_command_buffer->get_impl() == this));
        compute_encoder->barrierAfterEncoderStages(
            static_cast<MTL::Stages>(after_stages),
            static_cast<MTL::Stages>(compute_before),
            MTL4::VisibilityOptionDevice
        );
        if (scope == Barrier_scope::earlier_submissions) {
            encode_queue_stage_barrier(compute_encoder, after_stages, compute_before);
        }
    }
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
    add_barrier(get_image_usage_stages(usage_before), get_image_usage_stages(usage_after), scope);
}

void Command_buffer_impl::buffer_barrier(const Buffer& /*buffer*/, const std::uint64_t usage_before, const std::uint64_t usage_after, const Barrier_scope scope)
{
    add_barrier(get_buffer_usage_stages(usage_before), get_buffer_usage_stages(usage_after), scope);
}

void Command_buffer_impl::encode_start_barriers(MTL4::RenderCommandEncoder* const encoder, const Render_encoder_use use)
{
    ERHE_VERIFY(encoder != nullptr);
    if (is_queue_barrier_enabled()) {
        encode_queue_barrier(encoder);
    }
    if (m_pending_render_barrier.before_stages != 0) {
        encode_queue_stage_barrier(encoder, m_pending_render_barrier.after_stages, m_pending_render_barrier.before_stages);
        m_pending_render_barrier = Pending_barrier{};
    }
    if (use == Render_encoder_use::transfer) {
        // A transfer recorded as a render pass (blit_framebuffer's draw, a
        // depth / stencil clear): its transfer accesses are blit-stage
        // consumers of the pending compute barrier, ordered here as render
        // stages. The pending barrier stays for the compute encoders after it.
        if ((m_pending_compute_barrier.before_stages & MTL::StageBlit) != 0) {
            encode_queue_stage_barrier(encoder, m_pending_compute_barrier.after_stages, c_render_encoder_stages);
        }
    }
}

void Command_buffer_impl::encode_start_barriers(MTL4::ComputeCommandEncoder* const encoder)
{
    ERHE_VERIFY(encoder != nullptr);
    if (is_queue_barrier_enabled()) {
        encode_queue_barrier(encoder);
    }
    if (m_pending_compute_barrier.before_stages != 0) {
        encode_queue_stage_barrier(encoder, m_pending_compute_barrier.after_stages, m_pending_compute_barrier.before_stages);
        m_pending_compute_barrier = Pending_barrier{};
    }
}

void Command_buffer_impl::cmd_texture_barrier(std::uint64_t /*usage_before*/, std::uint64_t /*usage_after*/)
{
    // Every MTL4 encoder starts with a barrier on all earlier work of the
    // queue (encode_queue_barrier), which covers texture hazards between
    // encoders.
}

void Command_buffer_impl::memory_barrier(Memory_barrier_mask /*barriers*/)
{
    // Between encoders the queue barrier every encoder starts with already
    // orders the accesses. Inside an open compute encoder of this thread,
    // the in-encoder barrier.
    MTL4::ComputeCommandEncoder* const encoder = Compute_command_encoder_impl::get_active_mtl_encoder();
    if (encoder != nullptr) {
        encode_intra_encoder_barrier(encoder);
    }
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
    recording->command_buffer->endCommandBuffer();
    recording->gpu_timer_samples = m_gpu_timer_samples;
    m_gpu_timer_samples   = Gpu_timer_command_buffer_samples{};
    m_recording                 = nullptr;
    m_recording_used            = false;
    m_drawable_prefix_committed = false;
    m_inline_bytes_chunk        = nullptr;
    m_inline_bytes_offset = 0;
    // A barrier no encoder of this cb consumed orders nothing.
    if ((m_pending_render_barrier.before_stages != 0) || (m_pending_compute_barrier.before_stages != 0)) {
        log_render_pass->debug(
            "Metal 4 command buffer '{}': dropping unconsumed barriers render 0x{:x} -> 0x{:x}, compute 0x{:x} -> 0x{:x}",
            m_debug_label.data(),
            m_pending_render_barrier.after_stages,  m_pending_render_barrier.before_stages,
            m_pending_compute_barrier.after_stages, m_pending_compute_barrier.before_stages
        );
    }
    m_pending_render_barrier  = Pending_barrier{};
    m_pending_compute_barrier = Pending_barrier{};
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
