#pragma once

#include "erhe/geometry/geometry.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace editor {

auto parse_obj_geometry(const std::filesystem::path& path)
    -> std::vector<std::shared_ptr<erhe::geometry::Geometry>>;

}
