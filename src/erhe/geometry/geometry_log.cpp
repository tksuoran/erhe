#include "erhe/geometry/geometry_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::geometry
{

std::shared_ptr<spdlog::logger> log_geometry         ;
std::shared_ptr<spdlog::logger> log_build_edges      ;
std::shared_ptr<spdlog::logger> log_tangent_gen      ;
std::shared_ptr<spdlog::logger> log_cone             ;
std::shared_ptr<spdlog::logger> log_torus            ;
std::shared_ptr<spdlog::logger> log_sphere           ;
std::shared_ptr<spdlog::logger> log_polygon_texcoords;
std::shared_ptr<spdlog::logger> log_interpolate      ;
std::shared_ptr<spdlog::logger> log_operation        ;
std::shared_ptr<spdlog::logger> log_catmull_clark    ;
std::shared_ptr<spdlog::logger> log_triangulate      ;
std::shared_ptr<spdlog::logger> log_subdivide        ;
std::shared_ptr<spdlog::logger> log_attribute_maps   ;
std::shared_ptr<spdlog::logger> log_merge            ;
std::shared_ptr<spdlog::logger> log_weld             ;

void initialize_logging()
{
    using namespace erhe::log;
    log_geometry          = make_logger("erhe.geometry.geometry"         );
    log_build_edges       = make_logger("erhe.geometry.build_edges"      );
    log_tangent_gen       = make_logger("erhe.geometry.tangent_gen"      );
    log_cone              = make_logger("erhe.geometry.cone"             );
    log_torus             = make_logger("erhe.geometry.torus"            );
    log_sphere            = make_logger("erhe.geometry.sphere"           );
    log_polygon_texcoords = make_logger("erhe.geometry.polygon_texcoords");
    log_interpolate       = make_logger("erhe.geometry.interpolate"      );
    log_operation         = make_logger("erhe.geometry.operation"        );
    log_catmull_clark     = make_logger("erhe.geometry.catmull_clark"    );
    log_triangulate       = make_logger("erhe.geometry.triangulate"      );
    log_subdivide         = make_logger("erhe.geometry.subdivide"        );
    log_attribute_maps    = make_logger("erhe.geometry.attribute_maps"   );
    log_merge             = make_logger("erhe.geometry.merge"            );
    log_weld              = make_logger("erhe.geometry.weld"             );
}

} // namespace erhe::geometry
