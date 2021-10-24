#pragma once

#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/node.hpp"

#include <embree3/rtcore.h>

namespace erhe::raytrace
{

class Device;

class IGeometry
    : public erhe::scene::INode_attachment
{
public:
    //void enable();
    //void disable();
    //void set_mask(unsigned int mask);
    //void intersection_filter(const RTCFilterFunctionNArguments* args);

    static auto create(IDevice& device) -> IGeometry*;
    static auto create_shared(IDevice& device) -> std::shared_ptr<IGeometry>;
};

} // namespace erhe::raytrace
