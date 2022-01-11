#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "log.hpp"

#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

using erhe::scene::Light;
using erhe::scene::Light_layer;
using erhe::scene::Mesh;
using erhe::scene::Mesh_layer;
using erhe::scene::Scene;
using erhe::physics::IWorld;

void add_to_scene_layer(
    Scene&                scene,
    Mesh_layer&           layer,
    std::shared_ptr<Mesh> mesh
)
{
    ERHE_VERIFY(mesh);

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
    Scene&                 scene,
    Light_layer&           layer,
    std::shared_ptr<Light> light
)
{
    ERHE_VERIFY(light);

    log_scene.trace("add_to_scene_layer(light = {})\n", light->name());

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
    IWorld&                       physics_world,
    std::shared_ptr<Node_physics> node_physics
)
{
    ERHE_VERIFY(node_physics);

    log_scene.trace("add_to_physics_world()\n");

    physics_world.add_rigid_body(node_physics->rigid_body());
    node_physics->on_attached_to(&physics_world);
}

void add_to_raytrace_scene(
    erhe::raytrace::IScene&        raytrace_scene,
    std::shared_ptr<Node_raytrace> node_raytrace
)
{
    ERHE_VERIFY(node_raytrace);

    raytrace_scene.attach(node_raytrace->raytrace_instance());
}

void remove_from_scene_layer(
    Scene&                scene,
    Mesh_layer&           layer,
    std::shared_ptr<Mesh> mesh
)
{
    ERHE_VERIFY(mesh);

    {
        auto& meshes = layer.meshes;
        const auto i = std::remove(meshes.begin(), meshes.end(), mesh);
        if (i == meshes.end())
        {
            log_scene.error("mesh {} not in layer meshes\n", mesh->name());
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
            log_scene.error("mesh {} not in scene nodes\n", mesh->name());
        }
        else
        {
            nodes.erase(i, nodes.end());
        }
    }
}

void remove_from_physics_world(
    IWorld&       physics_world,
    Node_physics& node_physics
)
{
    physics_world.remove_rigid_body(node_physics.rigid_body());
    node_physics.on_detached_from(&physics_world);
}

void remove_from_raytrace_scene(
    erhe::raytrace::IScene&        raytrace_scene,
    std::shared_ptr<Node_raytrace> node_raytrace
)
{
    ERHE_VERIFY(node_raytrace);

    raytrace_scene.detach(node_raytrace->raytrace_geometry());
}

void remove_from_scene_layer(
    Scene&                 scene,
    Light_layer&           layer,
    std::shared_ptr<Light> light
)
{
    ERHE_VERIFY(light);

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