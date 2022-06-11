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


void add_to_physics_world(
    IWorld&                       physics_world,
    std::shared_ptr<Node_physics> node_physics
)
{
    ERHE_VERIFY(node_physics);

    log_scene->trace("add_to_physics_world()");

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

    raytrace_scene.detach(node_raytrace->raytrace_instance());
}

}