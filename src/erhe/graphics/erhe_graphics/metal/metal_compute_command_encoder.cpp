#include "erhe_graphics/metal/metal_compute_command_encoder.hpp"
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

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device)
    : Command_encoder_impl{device}
{
    // Create command buffer and compute encoder up front so that
    // set_buffer() can be called before set_compute_pipeline_state().
    Device_impl& device_impl = m_device.get_impl();
    MTL::CommandQueue* queue = device_impl.get_mtl_command_queue();
    ERHE_VERIFY(queue != nullptr);

    m_command_buffer = queue->commandBuffer();
    ERHE_VERIFY(m_command_buffer != nullptr);

    m_encoder = m_command_buffer->computeCommandEncoder();
    ERHE_VERIFY(m_encoder != nullptr);
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
    if (m_encoder != nullptr) {
        m_encoder->endEncoding();
        m_encoder = nullptr;
    }
    if (m_command_buffer != nullptr) {
        m_command_buffer->commit();
        m_command_buffer = nullptr;
    }
    if (m_pipeline_state != nullptr) {
        m_pipeline_state->release();
        m_pipeline_state = nullptr;
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
