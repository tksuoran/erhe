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
    log_geometry          = erhe::log::make_logger("erhe::geometry::geometry",          spdlog::level::trace);
    log_build_edges       = erhe::log::make_logger("erhe::geometry::build_edges",       spdlog::level::info);
    log_tangent_gen       = erhe::log::make_logger("erhe::geometry::tangent_gen",       spdlog::level::info);
    log_cone              = erhe::log::make_logger("erhe::geometry::cone",              spdlog::level::info);
    log_torus             = erhe::log::make_logger("erhe::geometry::torus",             spdlog::level::info);
    log_sphere            = erhe::log::make_logger("erhe::geometry::sphere",            spdlog::level::info);
    log_polygon_texcoords = erhe::log::make_logger("erhe::geometry::polygon_texcoords", spdlog::level::info);
    log_interpolate       = erhe::log::make_logger("erhe::geometry::interpolate",       spdlog::level::info);
    log_operation         = erhe::log::make_logger("erhe::geometry::operation",         spdlog::level::info);
    log_catmull_clark     = erhe::log::make_logger("erhe::geometry::catmull_clark",     spdlog::level::info);
    log_triangulate       = erhe::log::make_logger("erhe::geometry::triangulate",       spdlog::level::info);
    log_subdivide         = erhe::log::make_logger("erhe::geometry::subdivide",         spdlog::level::info);
    log_attribute_maps    = erhe::log::make_logger("erhe::geometry::attribute_maps",    spdlog::level::info);
    log_merge             = erhe::log::make_logger("erhe::geometry::merge",             spdlog::level::info);
    log_weld              = erhe::log::make_logger("erhe::geometry::weld",              spdlog::level::info);
}

} // namespace erhe::geometry
