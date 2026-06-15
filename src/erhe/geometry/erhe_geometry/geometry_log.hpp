#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::geometry {

extern std::shared_ptr<spdlog::logger> log_geometry;
extern std::shared_ptr<spdlog::logger> log_geogram;
extern std::shared_ptr<spdlog::logger> log_build_edges;
extern std::shared_ptr<spdlog::logger> log_tangent_gen;
extern std::shared_ptr<spdlog::logger> log_cone;
extern std::shared_ptr<spdlog::logger> log_torus;
extern std::shared_ptr<spdlog::logger> log_sphere;
extern std::shared_ptr<spdlog::logger> log_polygon_texcoords;
extern std::shared_ptr<spdlog::logger> log_interpolate;
extern std::shared_ptr<spdlog::logger> log_operation;
extern std::shared_ptr<spdlog::logger> log_catmull_clark;
extern std::shared_ptr<spdlog::logger> log_triangulate;
extern std::shared_ptr<spdlog::logger> log_subdivide;
extern std::shared_ptr<spdlog::logger> log_attribute_maps;
extern std::shared_ptr<spdlog::logger> log_merge;
extern std::shared_ptr<spdlog::logger> log_weld;

void initialize_logging();

// Routes Geogram's GEO::Logger output into log_geogram. Call once, AFTER both
// GEO::initialize() (which creates the Geogram Logger singleton) and
// initialize_logging() (which creates log_geogram). Geogram takes ownership of
// the forwarding client.
void register_geogram_logger();

// Detaches and destroys the forwarding client registered by
// register_geogram_logger(). Call during controlled shutdown while log_geogram
// is still alive, before the Geogram Logger or the erhe loggers are torn down.
// Safe to call when no client is registered.
void unregister_geogram_logger();

} // namespace erhe::geometry
