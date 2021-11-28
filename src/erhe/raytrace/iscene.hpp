#pragma once

#include <memory>

namespace erhe::raytrace
{

class IGeometry;

class IScene
{
public:
    virtual ~IScene(){};

    virtual void attach(IGeometry* geometry) = 0;
    virtual void detach(IGeometry* geometry) = 0;

    static auto create       () -> IScene*;
    static auto create_shared() -> std::shared_ptr<IScene>;
    static auto create_unique() -> std::unique_ptr<IScene>;
};

} // namespace erhe::raytrace
