#pragma once

#include "erhe_graphics/render_pipeline_state.hpp"

#include <functional>

namespace erhe::renderer {

class Pipeline_pass
{
public:
    void reset();

    explicit Pipeline_pass(erhe::graphics::Render_pipeline_state&& pipeline);
    Pipeline_pass(
        erhe::graphics::Render_pipeline_state&& pipeline,
        std::function<void()>                   begin,
        std::function<void()>                   end
    );

    erhe::graphics::Render_pipeline_state pipeline;
    std::function<void()>                 begin;
    std::function<void()>                 end;
};

} // namespace erhe::renderer
