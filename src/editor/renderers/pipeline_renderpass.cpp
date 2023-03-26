#include "renderers/pipeline_renderpass.hpp"

namespace editor
{

void Pipeline_renderpass::reset()
{
    pipeline.reset();
    begin = {};
    end = {};
}

} // namespace editor
