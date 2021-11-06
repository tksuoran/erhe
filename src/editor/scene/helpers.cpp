#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "log.hpp"

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

void add_to_scene_layer(
    Scene&           scene,
    Layer&           layer,
    shared_ptr<Mesh> mesh
)
{
    VERIFY(mesh);

    log_scene.trace("add_to_scene_layer(mesh = {})\n", mesh->name());

    {
        auto& meshes = layer.meshes;
#ifndef NDEBUG
        const auto i = std::find(meshes.begin(), meshes.end(), mesh);
        if (i != meshes.end())
        {
            log_scene.error("mesh {} already in layer meshes\n", mesh->name());
        }
        else
#endif
        {
            meshes.push_back(mesh);
        }
    }

    {
        auto& nodes = scene.nodes;
#ifndef NDEBUG
        const auto i = std::find(nodes.begin(), nodes.end(), mesh);
        if (i != nodes.end())
        {
            log_scene.error("mesh {} already in scene nodes\n", mesh->name());
        }
        else
#endif
        {
            nodes.push_back(mesh);
            scene.nodes_sorted = false;
        }
    }
}

void add_to_scene_layer(
    Scene&            scene,
    Layer&            layer,
    shared_ptr<Light> light
)
{
    VERIFY(light);

    log_scene.trace("add_to_scene_layer(trace = {})\n", light->name());

    {
        auto& lights = layer.lights;
#ifndef NDEBUG
        const auto i = std::find(lights.begin(), lights.end(), light);
        if (i != lights.end())
        {
            log_scene.error("light {} already in layer lights\n", light->name());
        }
        else
#endif
        {
            lights.push_back(light);
        }
    }
    {
        auto& nodes = scene.nodes;
#ifndef NDEBUG
        const auto i = std::find(nodes.begin(), nodes.end(), light);
        if (i != nodes.end())
        {
            log_scene.error("light {} already in scene nodes\n", light->name());
        }
        else
#endif
        {
            nodes.push_back(light);
            scene.nodes_sorted = false;
        }
    }
}

void add_to_physics_world(
    Scene&                   scene,
    IWorld&                  physics_world,
    shared_ptr<Node_physics> node_physics
)
{
    VERIFY(node_physics);

    log_scene.trace("add_to_physics_world(node_physics = {})\n", node_physics->name());

    {
        auto& nodes = scene.nodes;
#ifndef NDEBUG
        const auto i = std::find(nodes.begin(), nodes.end(), node_physics);
        if (i != nodes.end())
        {
            log_scene.error("node physics {} already in scene nodes\n", node_physics->name());
        }
        else
#endif
        {
            nodes.push_back(node_physics);
            scene.nodes_sorted = false;
        }
    }
    physics_world.add_rigid_body(node_physics->rigid_body());
}

void remove_from_scene_layer(
    Scene&           scene,
    Layer&           layer,
    shared_ptr<Mesh> mesh
)
{
    VERIFY(mesh);

    log_scene.trace("remove_from_scene_layer(mesh = {})\n", mesh->name());

    {
        auto& meshes = layer.meshes;
        const auto i = std::remove(meshes.begin(), meshes.end(), mesh);
        if (i == meshes.end()) 
        {
            log_scene.error("mesh {} not in layer meshes\n");
        }
        else
        {
            meshes.erase(i, meshes.end());
        }
    }

    {
        auto& nodes = scene.nodes;
        const auto i = std::remove(nodes.begin(), nodes.end(), mesh);
        if (i == nodes.end())
        {
            log_scene.error("mesh {} not in scene nodes\n");
        }
        else
        {
            nodes.erase(i, nodes.end());
        }
    }
}

void remove_from_physics_world(
    Scene&                   scene,
    IWorld&                  physics_world,
    shared_ptr<Node_physics> node_physics
)
{
    VERIFY(node_physics);

    log_scene.trace("remove_from_physics_world(node_physics = {})\n", node_physics->name());

    auto& nodes = scene.nodes;

    physics_world.remove_rigid_body(node_physics->rigid_body());

    const auto i = std::remove(nodes.begin(), nodes.end(), node_physics);
    if (i == nodes.end())
    {
        log_scene.error("node physics {} not in physics world\n", node_physics->name());
    }
    else
    {
        nodes.erase(i, nodes.end());
    }
}

void remove_from_scene_layer(
    Scene&            scene,
    Layer&            layer,
    shared_ptr<Light> light
)
{
    VERIFY(light);

    log_scene.trace("remove_from_scene_layer(light = {})\n", light->name());

    {
        auto& lights = layer.lights;
        const auto i = std::remove(lights.begin(), lights.end(), light);

        if (i == lights.end())
        {
            log_scene.error("light {} not in layer lights\n", light->name());
        }
        else
        {
            lights.erase(i, lights.end());
        }
    }

    {
        auto& nodes = scene.nodes;
        const auto i = std::remove(nodes.begin(), nodes.end(), light);
        if (i == nodes.end())
        {
            log_scene.error("light {} not in scene nodes\n", light->name());
        }
        else
        {
            nodes.erase(i, nodes.end());
        }
    }
}

}