#pragma once

namespace erhe::geometry
{
    class Geometry;
}

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace editor {

[[nodiscard]] auto parse_gltf(const std::filesystem::path& path)
-> std::vector<std::shared_ptr<erhe::geometry::Geometry>>;

}
