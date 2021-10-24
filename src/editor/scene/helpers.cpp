#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"

#include "erhe/physics/iworld.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

using namespace std;
using namespace erhe::geometry;
using namespace erhe::scene;
using namespace erhe::primitive;
using namespace erhe::physics;
using namespace std;

void attach(Layer&                   layer,
            Scene&                   scene,
            IWorld&                  physics_world,
            shared_ptr<Node>         node,
            shared_ptr<Mesh>         mesh,
            shared_ptr<Node_physics> node_physics)
{
    VERIFY(node);
    VERIFY(mesh);

    layer.meshes.push_back(mesh);
    scene.nodes.push_back(node);
    node->attach(mesh);
    if (node_physics)
    {
        node->attach(node_physics);
        physics_world.add_rigid_body(node_physics->rigid_body());
    }
    if (node->parent)
    {
        node->parent->attach(node);
    }
}

void detach(Layer&                   layer,
            Scene&                   scene,
            IWorld&                  physics_world,
            shared_ptr<Node>         node,
            shared_ptr<Mesh>         mesh,
            shared_ptr<Node_physics> node_physics)
{
    VERIFY(node);
    VERIFY(mesh);

    auto& meshes = layer.meshes;
    auto& nodes  = scene.nodes;

    if (node->parent)
    {
        node->parent->detach(node);
    }
    node->detach(mesh);
    if (node_physics)
    {
        node->detach(node_physics);
        physics_world.remove_rigid_body(node_physics->rigid_body());
    }
    meshes.erase(std::remove(meshes.begin(), meshes.end(), mesh), meshes.end());
    nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
}

void attach(Layer&            layer,
            Scene&            scene,
            shared_ptr<Node>  node,
            shared_ptr<Light> light)
{
    VERIFY(node);
    VERIFY(light);

    layer.lights.push_back(light);
    scene.nodes.push_back(node);
    node->attach(light);
}

void detach(Layer&            layer,
            Scene&            scene,
            shared_ptr<Node>  node,
            shared_ptr<Light> light)
{
    VERIFY(node);
    VERIFY(light);

    node->detach(light);
    layer.lights.erase(std::remove(layer.lights.begin(), layer.lights.end(), light), layer.lights.end());
    scene.nodes.erase(std::remove(scene.nodes.begin(), scene.nodes.end(), node), scene.nodes.end());
}

}