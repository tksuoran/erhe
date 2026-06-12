#include "erhe_physics/physics_joint_settings.hpp"

namespace erhe::physics {

Physics_joint_settings::Physics_joint_settings()                                         = default;
Physics_joint_settings::Physics_joint_settings(const Physics_joint_settings&)            = default;
Physics_joint_settings& Physics_joint_settings::operator=(const Physics_joint_settings&) = default;
Physics_joint_settings::~Physics_joint_settings() noexcept                               = default;

Physics_joint_settings::Physics_joint_settings(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

} // namespace erhe::physics
