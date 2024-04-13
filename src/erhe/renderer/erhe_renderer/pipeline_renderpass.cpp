#include "erhe_renderer/pipeline_renderpass.hpp"

namespace erhe::renderer
{

Pipeline_renderpass::Pipeline_renderpass(
    const std::shared_ptr<igl::IRenderPipelineState>& pipeline
)
    : pipeline{pipeline}
{
}

Pipeline_renderpass::Pipeline_renderpass(
    const std::shared_ptr<igl::IRenderPipelineState>& pipeline,
    std::function<void()>      begin,
    std::function<void()>      end
)
    : pipeline{pipeline}
    , begin{begin}
    , end{end}
{
}

void Pipeline_renderpass::reset()
{
    pipeline.reset();
    begin = {};
    end = {};
}

} // namespace erhe::renderer
