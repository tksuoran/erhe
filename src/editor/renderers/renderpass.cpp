#include "renderers/renderpass.hpp"

namespace editor
{

void Renderpass::reset()
{
    pipeline.reset();
    begin = {};
    end = {};
}

} // namespace editor
