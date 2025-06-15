#pragma once

#include "erhe_graphics/render_pipeline_state.hpp"

#include <functional>

namespace erhe::renderer {

class Pipeline_renderpass
{
public:
    void reset();

    explicit Pipeline_renderpass(erhe::graphics::Render_pipeline_state&& pipeline);
    Pipeline_renderpass(
        erhe::graphics::Render_pipeline_state&& pipeline,
        std::function<void()>                   begin,
        std::function<void()>                   end
    );

    erhe::graphics::Render_pipeline_state pipeline;
    std::function<void()>                 begin;
    std::function<void()>                 end;
};

} // namespace erhe::renderer
