#include "erhe/physics/log.hpp"

namespace erhe::physics
{

std::shared_ptr<spdlog::logger> log_physics      ;
std::shared_ptr<spdlog::logger> log_physics_frame;

void initialize_logging()
{
    log_physics       = erhe::log::make_logger("erhe::physics::physics",       spdlog::level::info);
    log_physics_frame = erhe::log::make_logger("erhe::physics::physics_frame", spdlog::level::info, false);
}

} // namespace erhe::physics
