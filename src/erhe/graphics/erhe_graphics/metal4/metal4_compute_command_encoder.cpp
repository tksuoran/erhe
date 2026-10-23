#include "erhe_graphics/metal4/metal4_compute_command_encoder.hpp"
#include "erhe_graphics/metal4/metal4_acceleration_structure.hpp"
#include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#include "erhe_graphics/metal4/metal4_compute_pipeline.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_indirect_count_draws.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/metal4/metal4_sampler.hpp"
#include "erhe_graphics/metal4/metal4_shader_stages.hpp"
#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_graphics/metal4/metal4_texture.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/metal4/metal4_bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

thread_local Compute_command_encoder_impl* Compute_command_encoder_impl::s_active_encoder{nullptr};

auto Compute_command_encoder_impl::get_active_mtl_encoder() noexcept -> MTL4::ComputeCommandEncoder*
{
    return (s_active_encoder != nullptr) ? s_active_encoder->m_encoder : nullptr;
}

auto Compute_command_encoder_impl::get_active_argument_table() noexcept -> MTL4::ArgumentTable*
{
    return (s_active_encoder != nullptr) ? s_active_encoder->m_argument_table : nullptr;
}

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : Command_encoder_impl{device, command_buffer}
{
    // Record into the MTL4::CommandBuffer that the caller's
    // Command_buffer owns. Metal only allows one encoder open on a cb
    // at a time; the API contract is that the caller does not interleave
    // encoders on the same cb.
    Command_buffer_impl& cb_impl = m_command_buffer.get_impl();
    m_mtl_command_buffer = cb_impl.get_mtl4_command_buffer();
    ERHE_VERIFY(m_mtl_command_buffer != nullptr);

    {
        Scoped_transient_object_pool object_pool{};
        m_encoder = m_mtl_command_buffer->computeCommandEncoder();
        ERHE_VERIFY(m_encoder != nullptr);
        m_encoder->retain();
    }
    s_active_encoder = this;

    // Order against all earlier work of the queue (doc/erhe/metal4_backend.md
    // "Barriers"): replaces the Metal 3 inter-encoder fence and hazard
    // tracking.
    encode_queue_barrier(m_encoder);

    // Gpu_timer samples (metal4_gpu_timer.hpp) at the start and end of the
    // encoder.
    {
        MTL4::CounterHeap* counter_heap = nullptr;
        std::size_t        start_sample = 0;
        std::size_t        end_sample   = 0;
        Gpu_timer_sample_pool* const pool = cb_impl.get_gpu_timer_sample_pool();
        if ((pool != nullptr) && cb_impl.get_gpu_timer_samples().begin_encoder(*pool, counter_heap, start_sample, end_sample)) {
            m_encoder->writeTimestamp(MTL4::TimestampGranularityRelaxed, counter_heap, start_sample);
            m_timer_counter_heap = counter_heap;
            m_timer_end_sample   = end_sample;
        }
    }

    m_argument_table = cb_impl.acquire_argument_table();
    m_argument_table->setAddress(
        m_device.get_impl().get_submission_pools().get_draw_id_zero(),
        c_metal_push_constant_index
    );
    m_encoder->setArgumentTable(m_argument_table);
}

void Compute_command_encoder_impl::before_dispatch()
{
    if (m_dispatch_count > 0) {
        encode_intra_encoder_barrier(m_encoder);
    }
    ++m_dispatch_count;
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
    if (s_active_encoder == this) {
        s_active_encoder = nullptr;
    }
    if (m_encoder != nullptr) {
        if (m_timer_counter_heap != nullptr) {
            m_encoder->writeTimestamp(MTL4::TimestampGranularityRelaxed, m_timer_counter_heap, m_timer_end_sample);
            m_timer_counter_heap = nullptr;
        }
        m_encoder->endEncoding();
        m_encoder->release();
        m_encoder = nullptr;
        m_command_buffer.get_impl().get_gpu_timer_samples().end_encoder();
    }
    // The cb is owned by Command_buffer, not by this encoder; nothing
    // to commit here. submit_command_buffers takes care of commit.
    release_owned_pipeline_state();
    m_mtl_command_buffer  = nullptr;
    m_argument_table      = nullptr;
}

void Compute_command_encoder_impl::release_owned_pipeline_state()
{
    // Only pipeline states created here (set_compute_pipeline_state, the
    // dynamic-state path) are owned; borrowed ones from a long-lived
    // Compute_pipeline_impl must not be released. An owned one is handed to
    // the command buffer, which releases it once the GPU has completed it
    // (MTL4 command buffers retain nothing), as the render path does.
    if ((m_pipeline_state != nullptr) && m_owns_pipeline_state) {
        m_command_buffer.get_impl().retain_until_completed(m_pipeline_state);
    }
    m_pipeline_state      = nullptr;
    m_owns_pipeline_state = false;
}

void Compute_command_encoder_impl::set_buffer(
    const Buffer_target  buffer_target,
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const std::uintptr_t index
)
{
    static_cast<void>(buffer_target);
    static_cast<void>(length);
    if (buffer == nullptr) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    // Identity mapping: GLSL binding = Metal buffer index
    m_argument_table->setAddress(mtl_buffer->gpuAddress() + static_cast<uint64_t>(offset), static_cast<NS::UInteger>(index));
}

void Compute_command_encoder_impl::set_buffer(const Buffer_target buffer_target, const Buffer* const buffer)
{
    static_cast<void>(buffer_target);
    if (buffer == nullptr) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    m_argument_table->setAddress(mtl_buffer->gpuAddress(), 0);
}

void Compute_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    // Tracked for set_sampled_image's sampler binding offset (mirrors
    // Render_command_encoder_impl).
    m_bind_group_layout = bind_group_layout;
}

void Compute_command_encoder_impl::set_storage_image(uint32_t binding_point, const Texture& texture)
{
    MTL::Texture* mtl_texture = texture.get_impl().get_mtl_texture();
    if (mtl_texture == nullptr) {
        return;
    }
    // Storage images bind at their raw binding point (no sampler-binding offset);
    // this matches the [[texture(N)]] slot pinned in compile_spirv_to_mtl_function.
    // The texture is resident through the device's residency set; the queue
    // barrier the next encoder starts with orders its writes.
    m_argument_table->setTexture(mtl_texture->gpuResourceID(), static_cast<NS::UInteger>(binding_point));
}

void Compute_command_encoder_impl::set_sampled_image(uint32_t binding_point, const Texture& texture, const Sampler& sampler)
{
    // Dedicated named samplers are direct [[texture(N)]]/[[sampler(M)]]
    // bindings on Metal; the texture slot is the user-facing binding_point plus
    // the bind group layout's sampler binding offset and the sampler slot is
    // the layout's compact dedicated-sampler index (Metal caps [[sampler(M)]]
    // at M <= 15), exactly like Render_command_encoder_impl::set_sampled_image
    // (the lightmap gather compute pass samples its G-buffer and sky LUTs this
    // way).
    ERHE_VERIFY(m_bind_group_layout != nullptr);
    const uint32_t glsl_binding = binding_point + m_bind_group_layout->get_sampler_binding_offset();
    const uint32_t sampler_slot = m_bind_group_layout->get_impl().get_metal_sampler_slot(glsl_binding);

    MTL::Texture*      mtl_texture = texture.get_impl().get_mtl_texture();
    MTL::SamplerState* mtl_sampler = sampler.get_impl().get_mtl_sampler();
    if ((mtl_texture == nullptr) || (mtl_sampler == nullptr)) {
        return;
    }
    m_argument_table->setTexture     (mtl_texture->gpuResourceID(), static_cast<NS::UInteger>(glsl_binding));
    m_argument_table->setSamplerState(mtl_sampler->gpuResourceID(), static_cast<NS::UInteger>(sampler_slot));
}

void Compute_command_encoder_impl::set_acceleration_structure(uint32_t binding_point, const Acceleration_structure& acceleration_structure)
{
    // The GLSL binding point is not the Metal slot: acceleration structure
    // resources are remapped to the reserved buffer index by
    // compile_spirv_to_mtl_function (UBOs/SSBOs own the identity-mapped
    // index namespace the binding point lives in).
    static_cast<void>(binding_point);
    const Acceleration_structure_impl& as_impl = acceleration_structure.get_impl();
    MTL::AccelerationStructure* tlas = as_impl.get_mtl_acceleration_structure();
    if (tlas == nullptr) {
        return;
    }
    // The instance structure and the bottom level structures it references
    // (by resource id) are resident through the device's residency set.
    m_argument_table->setResource(tlas->gpuResourceID(), c_metal_acceleration_structure_buffer_index);
}

void Compute_command_encoder_impl::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    const Compute_pipeline_data& data = pipeline.data;
    if (data.shader_stages == nullptr) {
        ERHE_FATAL("Metal: compute pipeline has no shader stages");
    }

    const Shader_stages_impl& stages_impl = data.shader_stages->get_impl();
    MTL::Function* compute_function = stages_impl.get_compute_function();
    if (compute_function == nullptr) {
        log_startup->error("Metal: no compute function for '{}'", data.shader_stages->name());
        ERHE_FATAL("Metal: no compute function");
    }
    m_threadgroup_size = stages_impl.get_compute_workgroup_size();

    Device_impl& device_impl = m_device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);

    release_owned_pipeline_state();
    NS::Error* error = nullptr;
    m_pipeline_state = mtl_device->newComputePipelineState(compute_function, &error);
    if (m_pipeline_state == nullptr) {
        const char* error_str = (error != nullptr) ? error->localizedDescription()->utf8String() : "unknown";
        ERHE_FATAL("Metal compute pipeline state creation failed: %s", error_str);
    }
    m_owns_pipeline_state = true;

    m_encoder->setComputePipelineState(m_pipeline_state);

    // Apply debug labels
    if (data.name != nullptr) {
        NS::String* label = NS::String::alloc()->init(
            data.name,
            NS::UTF8StringEncoding
        );
        m_mtl_command_buffer->setLabel(label);
        m_encoder->setLabel(label);
        label->release();
    }
}

void Compute_command_encoder_impl::set_compute_pipeline(const Compute_pipeline& pipeline)
{
    MTL::ComputePipelineState* mtl_pipeline = pipeline.get_impl().get_mtl_pipeline();
    if (mtl_pipeline == nullptr) {
        return;
    }
    // A pipeline an earlier set_compute_pipeline_state created goes to the
    // command buffer before m_pipeline_state is overwritten.
    release_owned_pipeline_state();
    m_pipeline_state      = mtl_pipeline;
    m_owns_pipeline_state = false;
    m_encoder->setComputePipelineState(m_pipeline_state);

    // Carry the shader's declared local workgroup size so dispatch_compute can
    // supply it to dispatchThreadgroups() (the erhe API passes group counts).
    const Shader_stages* shader_stages = pipeline.get_data().shader_stages;
    if (shader_stages != nullptr) {
        m_threadgroup_size = shader_stages->get_impl().get_compute_workgroup_size();
    }
}

void Compute_command_encoder_impl::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    ERHE_VERIFY(m_encoder != nullptr);
    ERHE_VERIFY(m_pipeline_state != nullptr);

    // x_size/y_size/z_size are workgroup COUNTS (GL/Vulkan dispatch semantics).
    // The per-group thread dimensions come from the shader's declared
    // layout(local_size_*), captured into m_threadgroup_size when the pipeline
    // was set. Metal's dispatchThreadgroups multiplies the two to get the grid.
    MTL::Size threads_per_grid = MTL::Size(
        static_cast<NS::UInteger>(x_size),
        static_cast<NS::UInteger>(y_size),
        static_cast<NS::UInteger>(z_size)
    );
    MTL::Size threads_per_threadgroup = MTL::Size(
        static_cast<NS::UInteger>(m_threadgroup_size[0]),
        static_cast<NS::UInteger>(m_threadgroup_size[1]),
        static_cast<NS::UInteger>(m_threadgroup_size[2])
    );

    before_dispatch();
    m_encoder->dispatchThreadgroups(threads_per_grid, threads_per_threadgroup);
}

void Compute_command_encoder_impl::encode_indirect_count_draws(Indirect_count_draws& draws, const std::size_t slot)
{
    ERHE_VERIFY(m_encoder != nullptr);
    // The kernel binds its own pipeline: drop the erhe one, so a further
    // dispatch_compute() without a new set_compute_pipeline() fails loudly
    // instead of running the kernel's pipeline.
    release_owned_pipeline_state();
    before_dispatch();
    draws.get_impl().encode(m_encoder, m_argument_table, draws.get_slot(slot), slot);
}

} // namespace erhe::graphics
