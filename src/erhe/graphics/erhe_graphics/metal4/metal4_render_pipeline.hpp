#pragma once

#include "erhe_graphics/render_pipeline.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

class Device;
class Device_impl;

class Render_pipeline_impl
{
public:
    Render_pipeline_impl(Device& device, const Render_pipeline_create_info& create_info);
    ~Render_pipeline_impl() noexcept;
    Render_pipeline_impl(const Render_pipeline_impl&) = delete;
    void operator=(const Render_pipeline_impl&) = delete;

    [[nodiscard]] auto get_pso           () const -> MTL::RenderPipelineState*;
    [[nodiscard]] auto get_depth_stencil () const -> MTL::DepthStencilState*;
    [[nodiscard]] auto is_valid          () const -> bool;

private:
    Device_impl&               m_device_impl;
    MTL::RenderPipelineState*  m_pso           {nullptr};
    MTL::DepthStencilState*    m_depth_stencil {nullptr};
};

} // namespace erhe::graphics
