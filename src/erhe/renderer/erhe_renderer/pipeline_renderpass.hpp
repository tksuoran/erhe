#pragma once

#include "igl/RenderPipelineState.h"
//#include "erhe_graphics/pipeline.hpp"

#include <functional>

namespace erhe::renderer
{

class Pipeline_renderpass
{
public:
    void reset();

    explicit Pipeline_renderpass(const std::shared_ptr<igl::IRenderPipelineState>& pipeline);
    Pipeline_renderpass(
        const std::shared_ptr<igl::IRenderPipelineState>& pipeline,
        std::function<void()>                             begin,
        std::function<void()>                             end
    );

    std::shared_ptr<igl::IRenderPipelineState> pipeline;
    std::function<void()>                      begin;
    std::function<void()>                      end;
};

} // namespace erhe::renderer
