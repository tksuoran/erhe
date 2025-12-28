#include "erhe_geometry/geometry_log.hpp"
#include "erhe_log/log.hpp"

#include <geogram/basic/logger.h>

namespace erhe::geometry {

std::shared_ptr<spdlog::logger> log_geometry         ;
std::shared_ptr<spdlog::logger> log_geogram          ;
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

class Geogram_logger_client : public GEO::LoggerClient {
public:
    ~Geogram_logger_client() noexcept override
    {
    }
    void div(const std::string& title) override {
        log_geogram->info(title);
    }
    void out(const std::string& str) override {
        log_geogram->info(str);
    }
    void warn  (const std::string& str) override {
        log_geogram->warn(str);
    }
    void err   (const std::string& str) override {
        log_geogram->error(str);
    }
    void status(const std::string& str) override {
        log_geogram->info(str);
    }
};

std::unique_ptr<Geogram_logger_client> s_geogram_logger_client;

void initialize_logging()
{
    using namespace erhe::log;
    log_geometry          = make_logger("erhe.geometry.geometry"         );
    log_geogram           = make_logger("erhe.geometry.geogram"          );
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
    s_geogram_logger_client = std::make_unique<Geogram_logger_client>();
}

} // namespace erhe::geometry
