#include "erhe/renderer/pipeline_renderpass.hpp"

namespace erhe::renderer
{

void Pipeline_renderpass::reset()
{
    pipeline.reset();
    begin = {};
    end = {};
}

} // namespace erhe::renderer
