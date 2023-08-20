#include "erhe_physics/physics_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::physics
{

std::shared_ptr<spdlog::logger> log_physics      ;
std::shared_ptr<spdlog::logger> log_physics_frame;

void initialize_logging()
{
    using namespace erhe::log;
    log_physics       = make_logger      ("erhe.physics.physics");
    log_physics_frame = make_frame_logger("erhe.physics.physics_frame");
}

} // namespace erhe::physics
