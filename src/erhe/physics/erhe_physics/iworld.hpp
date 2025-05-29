#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace erhe::renderer {
    class Jolt_debug_renderer;
}

namespace erhe::physics {

void initialize_physics_system();

class IConstraint;
class IRigid_body;
class IRigid_body_create_info;

class IWorld
{
public:
    virtual ~IWorld() noexcept;

    [[nodiscard]] static auto create       () -> IWorld*;
    [[nodiscard]] static auto create_shared() -> std::shared_ptr<IWorld>;
    [[nodiscard]] static auto create_unique() -> std::unique_ptr<IWorld>;

    [[nodiscard]] virtual auto create_rigid_body       (
        const IRigid_body_create_info& create_info,
        glm::vec3                      position    = glm::vec3{0.0f, 0.0f, 0.0f},
        glm::quat                      orientation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}
    ) -> IRigid_body* = 0;

    [[nodiscard]] virtual auto create_rigid_body_shared(
        const IRigid_body_create_info& create_info,
        glm::vec3                      position    = glm::vec3{0.0f, 0.0f, 0.0f},
        glm::quat                      orientation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}
    ) -> std::shared_ptr<IRigid_body> = 0;

    [[nodiscard]] virtual auto get_gravity         () const -> glm::vec3                 = 0;
    [[nodiscard]] virtual auto get_rigid_body_count() const -> std::size_t               = 0;
    [[nodiscard]] virtual auto get_constraint_count() const -> std::size_t               = 0;
    [[nodiscard]] virtual auto describe            () const -> std::vector<std::string>  = 0;
    virtual void update_fixed_step      (double dt)                                      = 0;
    virtual void add_rigid_body         (IRigid_body* rigid_body)                        = 0;
    virtual void remove_rigid_body      (IRigid_body* rigid_body)                        = 0;
    virtual void add_constraint         (IConstraint* constraint)                        = 0;
    virtual void remove_constraint      (IConstraint* constraint)                        = 0;
    virtual void set_gravity            (const glm::vec3& gravity)                       = 0;
    virtual void debug_draw             (erhe::renderer::Jolt_debug_renderer& debug_renderer) = 0;
    virtual void sanity_check           ()                                               = 0;
    virtual void set_on_body_activated  (std::function<void(IRigid_body*)> callback)     = 0;
    virtual void set_on_body_deactivated(std::function<void(IRigid_body*)> callback)     = 0;
    virtual void for_each_active_body   (std::function<void(IRigid_body*)> callback)     = 0;
};

} // namespace erhe::physics
