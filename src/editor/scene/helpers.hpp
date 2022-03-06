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

namespace erhe::raytrace
{
    class IScene;
}

namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Light;
    class Light_layer;
    class Mesh;
    class Mesh_layer;
    class Scene;
}

namespace erhe::physics
{
    class IWorld;
}

namespace editor
{

class Node_physics;
class Node_raytrace;

//void add_to_scene_layer(
//    erhe::scene::Scene&                scene,
//    erhe::scene::Mesh_layer&           layer,
//    std::shared_ptr<erhe::scene::Mesh> mesh_node
//);
//
//void add_to_scene_layer(
//    erhe::scene::Scene&                 scene,
//    erhe::scene::Light_layer&           layer,
//    std::shared_ptr<erhe::scene::Light> light_node
//);
//
//void add_to_scene(
//    erhe::scene::Scene&                  scene,
//    std::shared_ptr<erhe::scene::Camera> camera
//);

void add_to_physics_world(
    erhe::physics::IWorld&        physics_world,
    std::shared_ptr<Node_physics> node_physics
);

void add_to_raytrace_scene(
    erhe::raytrace::IScene&        raytrace_scene,
    std::shared_ptr<Node_raytrace> node_raytrace
);

void remove_from_physics_world(
    erhe::physics::IWorld&        physics_world,
    Node_physics&                 node_physics
);

void remove_from_raytrace_scene(
    erhe::raytrace::IScene&        raytrace_scene,
    std::shared_ptr<Node_raytrace> node_raytrace
);


} // namespace editor
