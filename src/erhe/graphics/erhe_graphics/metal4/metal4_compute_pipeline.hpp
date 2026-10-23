#pragma once

#include "erhe_graphics/compute_pipeline_state.hpp"

namespace MTL { class ComputePipelineState; }

namespace erhe::graphics {

class Device;
class Device_impl;

class Compute_pipeline_impl
{
public:
    Compute_pipeline_impl(Device& device, const Compute_pipeline_data& data);
    ~Compute_pipeline_impl() noexcept;

    [[nodiscard]] auto get_mtl_pipeline() const -> MTL::ComputePipelineState*;
    [[nodiscard]] auto is_valid        () const -> bool;

private:
    Device_impl&               m_device_impl;
    MTL::ComputePipelineState* m_pipeline_state{nullptr};
};

} // namespace erhe::graphics
