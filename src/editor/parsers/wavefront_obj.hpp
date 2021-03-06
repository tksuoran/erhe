#pragma once

#include "erhe/geometry/geometry.hpp"
#include <string>
#include <filesystem>

namespace editor {

erhe::geometry::Geometry parse_obj_geometry(const std::filesystem::path& path);

}
