#pragma once

// Plain-data description of glTF KHR_implicit_shapes + KHR_physics_rigid_bodies
// content. erhe::gltf carries these structs in and out of glTF files 1:1 with
// the glTF data model; the editor performs all mapping between these structs
// and erhe::physics / Node_physics. This header must stay data-only: glm + std
// types plus shared_ptr<erhe::scene::Node> references into the parsed node set.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::scene {
    class Node;
}

namespace erhe::gltf {

enum class Physics_shape_type : int {
    e_sphere,
    e_box,
    e_capsule,
    e_cylinder
};

// KHR_implicit_shapes.shapes[i]. All shapes are centered at the local origin
// and aligned along the Y axis. Capsule height is the distance between the
// centers of the two capping spheres (same convention as the erhe capsule
// shape length); cylinder height is the full axial height.
class Physics_shape
{
public:
    Physics_shape_type type         {Physics_shape_type::e_sphere};
    float              radius       {0.5f};             // sphere
    glm::vec3          size         {1.0f, 1.0f, 1.0f}; // box (full extents)
    float              height       {0.5f};             // capsule / cylinder
    float              radius_bottom{0.25f};
    float              radius_top   {0.25f};
};

enum class Physics_combine_mode : int {
    e_average,
    e_minimum,
    e_maximum,
    e_multiply
};

class Physics_material_description
{
public:
    std::string          name;
    float                static_friction    {0.6f};
    float                dynamic_friction   {0.6f};
    float                restitution        {0.0f};
    Physics_combine_mode friction_combine   {Physics_combine_mode::e_average};
    Physics_combine_mode restitution_combine{Physics_combine_mode::e_average};
};

class Physics_collision_filter_description
{
public:
    std::string              name;
    std::vector<std::string> collision_systems;
    std::vector<std::string> collide_with_systems;     // allowlist (exclusive with deny)
    std::vector<std::string> not_collide_with_systems; // denylist
};

class Physics_joint_limit
{
public:
    std::vector<int>     linear_axes;  // subset of {0, 1, 2}
    std::vector<int>     angular_axes; // subset of {0, 1, 2}
    std::optional<float> min;
    std::optional<float> max;
    std::optional<float> stiffness;
    float                damping{0.0f};
};

enum class Physics_drive_type : int {
    e_linear,
    e_angular
};

enum class Physics_drive_mode : int {
    e_force,
    e_acceleration
};

class Physics_joint_drive
{
public:
    Physics_drive_type type           {Physics_drive_type::e_linear};
    Physics_drive_mode mode           {Physics_drive_mode::e_force};
    int                axis           {0};
    float              max_force      {std::numeric_limits<float>::infinity()};
    float              position_target{0.0f};
    float              velocity_target{0.0f};
    float              stiffness      {0.0f};
    float              damping        {0.0f};
};

class Physics_joint_description
{
public:
    std::string                      name;
    std::vector<Physics_joint_limit> limits;
    std::vector<Physics_joint_drive> drives;
};

class Physics_node_motion
{
public:
    bool                     is_kinematic  {false};
    std::optional<float>     mass;
    glm::vec3                center_of_mass{0.0f};
    std::optional<glm::vec3> inertia_diagonal;
    std::optional<glm::quat> inertia_orientation;
    glm::vec3                linear_velocity {0.0f}; // node space (spec)
    glm::vec3                angular_velocity{0.0f}; // node space (spec)
    float                    gravity_factor  {1.0f};
};

class Physics_node_geometry
{
public:
    std::optional<std::size_t>         shape_index; // into Gltf_physics_data::shapes
    std::shared_ptr<erhe::scene::Node> node;        // mesh-providing node
    bool                               convex_hull{false};
};

class Physics_node_collider
{
public:
    Physics_node_geometry      geometry;
    std::optional<std::size_t> material_index; // into materials
    std::optional<std::size_t> filter_index;   // into collision_filters
};

class Physics_node_trigger
{
public:
    std::optional<Physics_node_geometry>            geometry;       // simple trigger
    std::optional<std::size_t>                      filter_index;
    std::vector<std::shared_ptr<erhe::scene::Node>> compound_nodes; // compound trigger
};

class Physics_node_joint
{
public:
    std::shared_ptr<erhe::scene::Node> connected_node;
    std::size_t                        joint_index{0}; // into joints
    bool                               enable_collision{false};
};

class Physics_node_description
{
public:
    std::shared_ptr<erhe::scene::Node>   node;
    std::optional<Physics_node_motion>   motion;
    std::optional<Physics_node_collider> collider;
    std::optional<Physics_node_trigger>  trigger;
    std::optional<Physics_node_joint>    joint;
};

class Gltf_physics_data
{
public:
    std::vector<Physics_shape>                        shapes;
    std::vector<Physics_material_description>         materials;
    std::vector<Physics_collision_filter_description> collision_filters;
    std::vector<Physics_joint_description>            joints;
    std::vector<Physics_node_description>             node_physics;
};

} // namespace erhe::gltf
