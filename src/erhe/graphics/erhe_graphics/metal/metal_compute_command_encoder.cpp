#include "erhe_graphics/metal/metal_compute_command_encoder.hpp"
#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_compute_pipeline.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_shader_stages.hpp"
#include "erhe_graphics/command_buffer.hpp"
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
    // Record into the MTL::CommandBuffer that the caller's
    // Command_buffer owns. Metal only allows one encoder open on a cb
    // at a time; the API contract is that the caller does not interleave
    // encoders on the same cb.
    Command_buffer_impl& cb_impl = m_command_buffer.get_impl();
    m_mtl_command_buffer  = cb_impl.get_mtl_command_buffer();
    m_inter_encoder_fence = cb_impl.get_inter_encoder_fence();
    ERHE_VERIFY(m_mtl_command_buffer != nullptr);

    m_encoder = m_mtl_command_buffer->computeCommandEncoder();
    ERHE_VERIFY(m_encoder != nullptr);

    // Serialize against prior encoders on this cb via the cb's
    // inter-encoder fence. Metal does not track aliased newTextureView
    // hazards within one cb; the fence catches those.
    if (m_inter_encoder_fence != nullptr) {
        m_encoder->waitForFence(m_inter_encoder_fence);
    }
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
    if (m_encoder != nullptr) {
        if (m_inter_encoder_fence != nullptr) {
            m_encoder->updateFence(m_inter_encoder_fence);
        }
        m_encoder->endEncoding();
        m_encoder = nullptr;
    }
    // The cb is owned by Command_buffer, not by this encoder; nothing
    // to commit here. submit_command_buffers takes care of commit.
    m_mtl_command_buffer  = nullptr;
    m_inter_encoder_fence = nullptr;
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

void Compute_command_encoder_impl::set_storage_image(uint32_t binding_point, const Texture& texture)
{
    // Storage-image compute is not wired on the Metal backend yet; the
    // atmosphere LUT generation that uses this is Vulkan-only for now.
    static_cast<void>(binding_point);
    static_cast<void>(texture);
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

    m_encoder->dispatchThreadgroups(threads_per_grid, threads_per_threadgroup);
}

} // namespace erhe::graphics
