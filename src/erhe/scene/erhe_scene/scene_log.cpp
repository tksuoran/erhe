#include "erhe_scene/scene_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::scene {

std::shared_ptr<spdlog::logger> log;
std::shared_ptr<spdlog::logger> log_frame;
std::shared_ptr<spdlog::logger> log_mesh_raytrace;

void initialize_logging()
{
    using namespace erhe::log;
    log               = make_logger      ("erhe.scene.log"          );
    log_frame         = make_frame_logger("erhe.scene.log_frame"    );
    log_mesh_raytrace = make_frame_logger("erhe.scene.mesh_raytrace");
}

} // namespace erhe::scene
