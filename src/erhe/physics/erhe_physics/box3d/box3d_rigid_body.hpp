#pragma once

#include "erhe_physics/box3d/box3d_collision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"

#include <box3d/box3d.h>

#include <string>
#include <vector>

namespace erhe::physics {

class Box3d_world;

class Box3d_rigid_body : public IRigid_body
{
public:
    Box3d_rigid_body(Box3d_world& world, const IRigid_body_create_info& create_info);
    ~Box3d_rigid_body() noexcept override;

    Box3d_rigid_body           (const Box3d_rigid_body&) = delete;
    Box3d_rigid_body& operator=(const Box3d_rigid_body&) = delete;

    // Implements IRigid_body
    auto get_angular_damping        () const -> float                              override;
    auto get_angular_velocity       () const -> glm::vec3                          override;
    auto get_center_of_mass         () const -> glm::vec3                          override;
    auto get_center_of_mass_transform() const -> Transform                         override;
    auto get_collision_shape        () const -> std::shared_ptr<ICollision_shape>  override;
    auto get_debug_label            () const -> const char*                        override;
    auto get_friction               () const -> float                              override;
    auto get_gravity_factor         () const -> float                              override;
    auto get_linear_damping         () const -> float                              override;
    auto get_linear_velocity        () const -> glm::vec3                          override;
    auto get_local_inertia          () const -> glm::mat4                          override;
    auto get_mass                   () const -> float                              override;
    auto get_motion_mode            () const -> Motion_mode                        override;
    auto get_restitution            () const -> float                              override;
    auto get_world_transform        () const -> glm::mat4                          override;
    auto is_active                  () const -> bool                               override;
    auto get_allow_sleeping         () const -> bool                               override;
    auto get_physics_material       () const -> std::shared_ptr<Physics_material>  override;
    auto get_collision_filter       () const -> std::shared_ptr<Collision_filter>  override;

    void begin_move          ()                                                    override;
    void end_move            ()                                                    override;
    void set_angular_velocity(const glm::vec3& velocity)                           override;
    void set_damping         (float linear_damping, float angular_damping)         override;
    void set_friction        (float friction)                                      override;
    void set_gravity_factor  (float gravity_factor)                                override;
    void set_linear_velocity (const glm::vec3& velocity)                           override;
    void set_mass_properties (float mass, const glm::mat4& local_inertia)          override;
    void set_motion_mode     (Motion_mode motion_mode)                             override;
    void set_restitution     (float restitution)                                   override;
    void set_world_transform (const Transform& transform)                          override;
    void teleport            (const Transform& transform)                          override;
    void set_allow_sleeping  (bool value)                                          override;
    void set_owner           (void* owner)                                         override;
    auto get_owner           () const -> void*                                     override;
    void set_physics_material(const std::shared_ptr<Physics_material>& material)   override;
    void set_collision_filter(const std::shared_ptr<Collision_filter>& filter)     override;

    // Box3D specific
    [[nodiscard]] auto get_box3d_body     () const -> b3BodyId                                { return m_body; }
    [[nodiscard]] auto get_world          () const -> Box3d_world&                            { return m_world; }
    [[nodiscard]] auto get_shape_ids      () const -> const std::vector<b3ShapeId>&            { return m_shape_ids; }
    [[nodiscard]] auto get_filter_index   () const -> int                                      { return m_filter_index; }
    [[nodiscard]] auto is_valid           () const -> bool                                     { return m_is_valid; }

    // The world enables a body when it is added to the simulation and disables
    // it when removed, mirroring erhe's create-then-add lifetime. Box3D itself
    // puts a body in the world the moment b3CreateBody returns.
    void set_enabled_in_world(bool enabled);

    // Awake state as last reported to the activation callbacks. Box3D has no
    // activation listener, so Box3d_world synthesizes activate / deactivate by
    // diffing this against the per-step b3BodyMoveEvent stream.
    [[nodiscard]] auto get_reported_awake() const -> bool { return m_reported_awake; }
    void set_reported_awake(bool value) { m_reported_awake = value; }

private:
    void attach_shapes  (b3ShapeDef& shape_def);
    void apply_mass     (const IRigid_body_create_info& create_info);

    Box3d_world&                      m_world;
    b3BodyId                          m_body            {};
    std::shared_ptr<ICollision_shape> m_collision_shape {};
    std::shared_ptr<Physics_material> m_physics_material{};
    std::shared_ptr<Collision_filter> m_collision_filter{};
    std::string                       m_debug_label     {};
    Motion_mode                       m_motion_mode     {Motion_mode::e_dynamic};
    void*                             m_owner           {nullptr};
    bool                              m_allow_sleeping  {true};
    bool                              m_is_sensor       {false};
    bool                              m_enable_collisions{true};
    bool                              m_is_valid        {false};
    bool                              m_reported_awake  {false};
    int                               m_filter_index    {-1};

    // Box3D resources owned by this body.
    std::vector<b3ShapeId>            m_shape_ids       {};
    std::vector<Box3d_hull>           m_derived_hulls   {};
};

} // namespace erhe::physics
