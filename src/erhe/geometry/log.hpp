#pragma once

#include "erhe/log/log.hpp"

namespace erhe::geometry
{

extern std::shared_ptr<spdlog::logger> log_geometry;
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

} // namespace erhe::geometry
