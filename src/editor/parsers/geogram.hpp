#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace erhe::primitive {
    class Build_info;
}

namespace editor {

class Scene_root;

void import_geogram(
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
);

}
