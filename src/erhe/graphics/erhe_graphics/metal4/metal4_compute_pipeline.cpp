#include "erhe_graphics/metal4/metal4_compute_pipeline.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_shader_stages.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

Compute_pipeline_impl::Compute_pipeline_impl(Device& device, const Compute_pipeline_data& data)
    : m_device_impl{device.get_impl()}
{
    Shader_stages* shader_stages = data.shader_stages;
    if (shader_stages == nullptr) {
        return;
    }

    const Shader_stages_impl& stages_impl = shader_stages->get_impl();
    MTL::Function* compute_function = stages_impl.get_compute_function();
    if (compute_function == nullptr) {
        log_startup->error("Metal: no compute function for '{}'", shader_stages->name());
        return;
    }

    Device_impl& device_impl = device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);

    NS::Error* error = nullptr;
    m_pipeline_state = mtl_device->newComputePipelineState(compute_function, &error);
    if (m_pipeline_state == nullptr) {
        const char* error_str = (error != nullptr) ? error->localizedDescription()->utf8String() : "unknown";
        log_startup->error("Metal compute pipeline state creation failed: {}", error_str);
    }
}

Compute_pipeline_impl::~Compute_pipeline_impl() noexcept
{
    if (m_pipeline_state != nullptr) {
        MTL::ComputePipelineState* pipeline_state = m_pipeline_state;
        m_pipeline_state = nullptr;
        m_device_impl.add_completion_handler(
            [pipeline_state](Device_impl&) {
                pipeline_state->release();
            }
        );
    }
}

auto Compute_pipeline_impl::get_mtl_pipeline() const -> MTL::ComputePipelineState*
{
    return m_pipeline_state;
}

auto Compute_pipeline_impl::is_valid() const -> bool
{
    return m_pipeline_state != nullptr;
}

} // namespace erhe::graphics
