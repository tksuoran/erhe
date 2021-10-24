#pragma once

#include <memory>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{
    class Material;
    class Primitive;
    class Primitive_build_context;
    class Primitive_geometry;
}

namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Layer;
    class Light;
    class Mesh;
    class Node;
    class Scene;
}

namespace erhe::physics
{
    class IWorld;
}

namespace editor
{

class Node_physics;

void attach(
    erhe::scene::Layer&                layer,
    erhe::scene::Scene&                scene,
    erhe::physics::IWorld&             physics_world,
    std::shared_ptr<erhe::scene::Node> node,
    std::shared_ptr<erhe::scene::Mesh> mesh,
    std::shared_ptr<Node_physics>      node_physics
);

void detach(
    erhe::scene::Layer&                layer,
    erhe::scene::Scene&                scene,
    erhe::physics::IWorld&             physics_world,
    std::shared_ptr<erhe::scene::Node> node,
    std::shared_ptr<erhe::scene::Mesh> mesh,
    std::shared_ptr<Node_physics>      node_physics
);

void attach(
    erhe::scene::Layer&                 layer,
    erhe::scene::Scene&                 scene,
    std::shared_ptr<erhe::scene::Node>  node,
    std::shared_ptr<erhe::scene::Light> light
);

void detach(
    erhe::scene::Layer&                 layer,
    erhe::scene::Scene&                 scene,
    std::shared_ptr<erhe::scene::Node>  node,
    std::shared_ptr<erhe::scene::Light> light
);


} // namespace editor
