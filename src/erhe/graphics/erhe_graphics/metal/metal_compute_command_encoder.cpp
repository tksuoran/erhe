#include "erhe_graphics/metal/metal_compute_command_encoder.hpp"
#include "erhe_graphics/metal/metal_compute_pipeline.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_shader_stages.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : Command_encoder_impl{device, command_buffer}
{
    // Create command buffer and compute encoder up front so that
    // set_buffer() can be called before set_compute_pipeline_state().
    //
    // Prefer the device-frame cb when one is active so compute work
    // lands in the single per-frame commit. In that case we hold the
    // Device_impl::m_recording_mutex for the lifetime of this encoder
    // so other encoders (render pass, blit) can't interleave with
    // our MTLComputeCommandEncoder - Metal only allows one encoder
    // open on a cb at a time.
    Device_impl& device_impl = m_device.get_impl();
    MTL::CommandBuffer* active = device_impl.get_device_frame_command_buffer();
    if (active != nullptr) {
        m_recording_lock = std::unique_lock<std::mutex>{device_impl.m_recording_mutex};
        m_command_buffer = active;
        m_owns_command_buffer = false;
    } else {
        MTL::CommandQueue* queue = device_impl.get_mtl_command_queue();
        ERHE_VERIFY(queue != nullptr);
        m_command_buffer = queue->commandBuffer();
        ERHE_VERIFY(m_command_buffer != nullptr);
        m_owns_command_buffer = true;
    }

    m_encoder = m_command_buffer->computeCommandEncoder();
    ERHE_VERIFY(m_encoder != nullptr);

    // When recording into the device cb, serialize against prior
    // encoders via the per-device-frame fence. See Device_impl header
    // for the rationale (Metal does not track aliased newTextureView
    // hazards within one cb).
    if (!m_owns_command_buffer) {
        MTL::Fence* fence = device_impl.get_device_frame_fence();
        if (fence != nullptr) {
            m_encoder->waitForFence(fence);
        }
    }
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
    if (m_encoder != nullptr) {
        if (!m_owns_command_buffer) {
            MTL::Fence* fence = m_device.get_impl().get_device_frame_fence();
            if (fence != nullptr) {
                m_encoder->updateFence(fence);
            }
        }
        m_encoder->endEncoding();
        m_encoder = nullptr;
    }
    if (m_command_buffer != nullptr) {
        if (m_owns_command_buffer) {
            m_command_buffer->commit();
        }
        m_command_buffer = nullptr;
    }
    if (m_pipeline_state != nullptr) {
        if (m_owns_pipeline_state) {
            // Only release pipeline states we created via newComputePipelineState
            // (the dynamic-state path). Borrowed pointers from a long-lived
            // Compute_pipeline_impl must NOT be released here -- doing so was
            // a use-after-free that crashed the next dispatch. Even for the
            // owned case, defer the release via add_completion_handler so the
            // GPU has a chance to finish using the pipeline.
            MTL::ComputePipelineState* ps = m_pipeline_state;
            m_device.get_impl().add_completion_handler(
                [ps](Device_impl&) {
                    ps->release();
                }
            );
        }
        m_pipeline_state = nullptr;
        m_owns_pipeline_state = false;
    }
}

void Compute_command_encoder_impl::set_buffer(
    const Buffer_target  buffer_target,
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const std::uintptr_t index
)
{
    static_cast<void>(length);
    if (buffer == nullptr) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    // Identity mapping: GLSL binding = Metal buffer index
    m_encoder->setBuffer(mtl_buffer, static_cast<NS::UInteger>(offset), static_cast<NS::UInteger>(index));
}

void Compute_command_encoder_impl::set_buffer(const Buffer_target buffer_target, const Buffer* const buffer)
{
    if (buffer == nullptr) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    m_encoder->setBuffer(mtl_buffer, 0, 0);
}

void Compute_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    static_cast<void>(bind_group_layout);
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

    Device_impl& device_impl = m_device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);

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
        m_command_buffer->setLabel(label);
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
    // If a previous set_compute_pipeline_state created a pipeline that this
    // encoder owns, defer-release it now -- we are about to overwrite
    // m_pipeline_state with a borrowed pointer.
    if ((m_pipeline_state != nullptr) && m_owns_pipeline_state) {
        MTL::ComputePipelineState* ps = m_pipeline_state;
        m_device.get_impl().add_completion_handler(
            [ps](Device_impl&) {
                ps->release();
            }
        );
    }
    m_pipeline_state      = mtl_pipeline;
    m_owns_pipeline_state = false;
    m_encoder->setComputePipelineState(m_pipeline_state);
}

void Compute_command_encoder_impl::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    ERHE_VERIFY(m_encoder != nullptr);
    ERHE_VERIFY(m_pipeline_state != nullptr);

    NS::UInteger thread_execution_width = m_pipeline_state->threadExecutionWidth();
    NS::UInteger max_total_threads      = m_pipeline_state->maxTotalThreadsPerThreadgroup();

    MTL::Size threads_per_grid        = MTL::Size(static_cast<NS::UInteger>(x_size), static_cast<NS::UInteger>(y_size), static_cast<NS::UInteger>(z_size));
    MTL::Size threads_per_threadgroup = MTL::Size(std::min(thread_execution_width, max_total_threads), 1, 1);

    m_encoder->dispatchThreadgroups(threads_per_grid, threads_per_threadgroup);
}

} // namespace erhe::graphics
