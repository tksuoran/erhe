#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::raytrace
{

std::shared_ptr<spdlog::logger> log_buffer  ;
std::shared_ptr<spdlog::logger> log_device  ;
std::shared_ptr<spdlog::logger> log_geometry;
std::shared_ptr<spdlog::logger> log_instance;
std::shared_ptr<spdlog::logger> log_scene   ;
std::shared_ptr<spdlog::logger> log_embree  ;
std::shared_ptr<spdlog::logger> log_frame   ;

void initialize_logging()
{
    using namespace erhe::log;
    log_buffer   = make_logger      ("erhe.raytrace.buffer"  );
    log_device   = make_logger      ("erhe.raytrace.device"  );
    log_geometry = make_logger      ("erhe.raytrace.geometry");
    log_instance = make_logger      ("erhe.raytrace.instance");
    log_scene    = make_logger      ("erhe.raytrace.scene"   );
    log_embree   = make_logger      ("erhe.raytrace.embree"  );
    log_frame    = make_frame_logger("erhe.raytrace.frame"   );
}

} // namespace erhe::raytrace
