#pragma once

#include <functional>
#include <memory>

namespace erhe::geometry
{
    class Geometry;
};

class btCollisionShape;

namespace editor
{

using Collision_shape_generator   = std::function<std::shared_ptr<btCollisionShape>(float)>;
using Collision_volume_calculator = std::function<float(float)>;

}
