#pragma once

#include "erhe_physics/iworld.hpp"

#include <memory>
#include <vector>

namespace erhe::physics {

class ICollision_shape;

class Null_world
    : public IWorld
{
public:
    virtual ~Null_world() noexcept override;

    // Implements IWorld
    // Implements IWorld
    auto create_rigid_body(
        const IRigid_body_create_info& create_info,
        glm::vec3                      position    = glm::vec3{0.0f, 0.0f, 0.0f},
        glm::quat                      orientation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}
    ) -> IRigid_body*                 override;
    auto create_rigid_body_shared(
        const IRigid_body_create_info& create_info,
        glm::vec3                      position    = glm::vec3{0.0f, 0.0f, 0.0f},
        glm::quat                      orientation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}
    ) -> std::shared_ptr<IRigid_body> override;

    auto get_gravity         () const -> glm::vec3                override;
    auto get_rigid_body_count() const -> std::size_t              override;
    auto get_constraint_count() const -> std::size_t              override;
    auto describe            () const -> std::vector<std::string> override;
    void update_fixed_step   (double dt)                          override;
    void set_gravity         (const glm::vec3& gravity)           override;
    void add_rigid_body      (IRigid_body* rigid_body)            override;
    void remove_rigid_body   (IRigid_body* rigid_body)            override;
    void add_constraint      (IConstraint* constraint)            override;
    void remove_constraint   (IConstraint* constraint)            override;
    void debug_draw          (erhe::renderer::Debug_renderer& debug_renderer)    override;
    void sanity_check        ()                                   override;

    void set_on_body_activated  (std::function<void(IRigid_body*)> callback) override;
    void set_on_body_deactivated(std::function<void(IRigid_body*)> callback) override;
    void for_each_active_body   (std::function<void(IRigid_body*)> callback) override;

private:
    glm::vec3                 m_gravity        {0.0f};

    std::vector<IRigid_body*> m_rigid_bodies;
    std::vector<IConstraint*> m_constraints;

    std::vector<std::shared_ptr<ICollision_shape>> m_collision_shapes;
};

} // namespace erhe::physics
