#pragma once

#include "erhe/graphics/pipeline.hpp"
#include "erhe/primitive/enums.hpp"

#include <functional>

namespace editor
{

class Renderpass
{
public:
    void reset();

    erhe::graphics::Pipeline        pipeline;
    erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    std::function<void()>           begin;
    std::function<void()>           end;
};

} // namespace editor
