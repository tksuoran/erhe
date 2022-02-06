#pragma once

namespace erhe::geometry
{
    class Geometry;
}

#include "erhe/toolkit/filesystem.hpp"

#include <memory>
#include <string>
#include <vector>

namespace editor {

[[nodiscard]] auto parse_obj_geometry(
    const fs::path& path
) -> std::vector<std::shared_ptr<erhe::geometry::Geometry>>;

}
