#pragma once

#include "erhe_graphics/pipeline.hpp"

#include <functional>

namespace erhe::renderer {

class Pipeline_renderpass
{
public:
    void reset();

    explicit Pipeline_renderpass(erhe::graphics::Pipeline&& pipeline);
    Pipeline_renderpass(
        erhe::graphics::Pipeline&& pipeline,
        std::function<void()>      begin,
        std::function<void()>      end
    );

    erhe::graphics::Pipeline pipeline;
    std::function<void()>    begin;
    std::function<void()>    end;
};

} // namespace erhe::renderer
