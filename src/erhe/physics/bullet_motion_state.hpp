#error

#include "erhe/physics/imotion_state.hpp"

#include "btBulletCollisionCommon.h"

#include <glm/glm.hpp>

namespace erhe::physics
{

class Bullet_motion_state
	: public IMotion_state
{
public:
	~Bullet_motion_state() override;

	// Implements IMotion_state
    auto get_world_transform_basis () const -> glm::mat3    override;
    auto get_world_transform_origin() const -> glm::vec3    override;
    void set_world_transform_basis (const glm::mat3 basis)  override;
    void set_world_transform_origin(const glm::vec3 origin) override;
};

} // namespace erhe::physics
