#pragma once

#include <functional>
#include <memory>

namespace erhe::geometry
{
    class Geometry;
};

namespace erhe::physics
{
    class ICollision_shape;
};

namespace editor
{

using Collision_shape_generator   = std::function<std::shared_ptr<erhe::physics::ICollision_shape>(float)>;
using Collision_volume_calculator = std::function<float(float)>;

}
