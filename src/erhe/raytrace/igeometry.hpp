#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::raytrace
{

class IScene;

class IGeometry
{
public:
    virtual ~IGeometry(){};

    virtual void set_transform(const glm::mat4 transform) = 0;

    static auto create       (IScene* scene) -> IGeometry*;
    static auto create_shared(IScene* scene) -> std::shared_ptr<IGeometry>;
};

} // namespace erhe::raytrace
