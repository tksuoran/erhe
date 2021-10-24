#pragma once

#include <memory>

namespace erhe::raytrace
{

class IDevice
{
public:
    static auto create() -> IDevice*;
    static auto create_shared() -> std::shared_ptr<IDevice>;
};

} // namespace erhe::raytrace
