#pragma once

#include "erhe/geometry/geometry.hpp"
#include <string>
#include <filesystem>

namespace editor {

erhe::geometry::Geometry make_json_polyhedron(const std::filesystem::path& path);

}
